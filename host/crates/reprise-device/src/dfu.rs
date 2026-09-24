// SPDX-License-Identifier: GPL-3.0-only
// Read-probe protocol adapted from https://github.com/freemyipod/wInd3x:
// pkg/exploit/exploit.go and pkg/exploit/dumpmem (GPL-2.0-or-later).
// Copyright (C) 2022 Serge 'q3k' Bazanski (q3k@q3k.org).

use crate::{exact, invalid, payload, syscfg, Result, SysCfg, BOOTROM_SIZE};
use sha2::{Digest, Sha256};

// Fingerprint spans covering cache control, dispatcher return, and EP0 replies.
pub(crate) const FAST_ROM_WINDOWS: [(usize, usize); 4] = [
    (0x400, 0x80),
    (0x4800, 0x100),
    (0xa500, 0x200),
    (0xa8c0, 0x200),
];
const FAST_ROM_WINDOWS_SHA256: &str =
    "275b874cadb2ab1476e1851b1850438771faf95f7210b11fa2cce4ab2e167fcf";

pub(crate) trait Transport {
    fn read(
        &mut self,
        request_type: u8,
        request: u8,
        value: u16,
        index: u16,
        data: &mut [u8],
    ) -> Result<usize>;
    fn write(
        &mut self,
        request_type: u8,
        request: u8,
        value: u16,
        index: u16,
        data: &[u8],
    ) -> Result<usize>;
}

pub(crate) struct Dfu<T> {
    pub(crate) transport: T,
}

impl<T: Transport> Dfu<T> {
    // Fast replies contain a 16-byte status/reserved header followed by data.
    pub(crate) fn fast_exchange(&mut self, wire: &[u8], data_size: usize) -> Result<Vec<u8>> {
        if wire.len() <= payload::PREFIX_SIZE
            || wire.len() > 0x400
            || !(1..=512).contains(&data_size)
        {
            return Err(invalid("Invalid fast reply layout"));
        }
        let mut response = self.reply(wire, data_size + 16)?;
        let status = u32::from_le_bytes(response[..4].try_into().unwrap());
        if status != 0 {
            return Err(invalid(format!("Fast probe ROM status {status:#x}")));
        }
        Ok(response.split_off(16))
    }

    pub(crate) fn reply(&mut self, wire: &[u8], response_size: usize) -> Result<Vec<u8>> {
        if wire.len() <= payload::PREFIX_SIZE
            || wire.len() > 0x400
            || !(1..=528).contains(&response_size)
        {
            return Err(invalid("Invalid probe reply layout"));
        }
        self.rom_clean()?;
        let n = self.transport.write(0x21, 1, 0, 0, wire)?;
        exact("Fast probe download", n, wire.len())?;
        self.rom_clean()?;
        let n = self.transport.read(0xa1, 2, 0, 0, &mut [0; 64])?;
        exact("Fast probe initial upload", n, 64)?;
        let mut response = vec![0xa5; response_size];
        let n = self.transport.read(0xa0, 0xfe, 0xeaff, 6, &mut response)?;
        exact("Fast probe response", n, response.len())?;
        Ok(response)
    }

    pub(crate) fn fast_echo(&mut self, mut emit: impl FnMut(usize) -> Result<()>) -> Result<()> {
        let sample: Vec<u8> = (0..512).map(|i| (i * 37 + i / 19) as u8).collect();
        for size in [48, 64, 112, 512] {
            let output =
                self.aes_exchange(&sample[..size], &[0; 16], &payload::fast_aes(size, false)?)?;
            if output != sample[..size] {
                return Err(invalid(format!(
                    "Echo mismatch for {}-byte reply",
                    size + 16
                )));
            }
            emit(size + 16)?;
        }
        Ok(())
    }

    pub(crate) fn state(&mut self) -> Result<u8> {
        let mut response = [0];
        let n = self.transport.read(0xa1, 5, 0, 0, &mut response)?;
        exact("DFU GETSTATE", n, 1)?;
        Ok(response[0])
    }

    fn command(&mut self, request: u8) -> Result<()> {
        let n = self.transport.write(0x21, request, 0, 0, &[])?;
        exact("DFU command", n, 0)
    }

    pub(crate) fn idle(&mut self) -> Result<()> {
        match self.state()? {
            2 => return Ok(()),
            10 => self.command(4)?,        // CLRSTATUS from dfuERROR
            3 | 5 | 9 => self.command(6)?, // ABORT from sync/download/upload idle
            state => {
                return Err(invalid(format!(
                    "Cannot clean DFU state {state}; re-enter fresh DFU"
                )))
            }
        }
        let state = self.state()?;
        if state != 2 {
            return Err(invalid(format!(
                "DFU cleanup ended in state {state}, expected dfuIDLE (2)"
            )));
        }
        Ok(())
    }

    // S5L8702 CLRSTATUS resets transfer bookkeeping even outside dfuERROR.
    // The exploit depends on this ROM-specific behavior.
    pub(crate) fn rom_clean(&mut self) -> Result<()> {
        self.command(4)?;
        let state = self.state()?;
        if state != 2 {
            return Err(invalid(format!("Probe cleanup returned DFU state {state}")));
        }
        Ok(())
    }

    pub(crate) fn execute(&mut self, payload: &[u8]) -> Result<[u8; 64]> {
        if payload.len() + payload::PREFIX_SIZE > 0x400 {
            return Err(invalid("Read payload exceeds DFU buffer"));
        }
        self.rom_clean()?;
        let mut download = vec![b'Z'; payload::PREFIX_SIZE];
        download.extend_from_slice(payload);
        let n = self.transport.write(0x21, 1, 0, 0, &download)?;
        exact("Probe download", n, download.len())?;
        self.rom_clean()?;
        let mut response = [0; 64];
        let n = self.transport.read(0xa1, 2, 0, 0, &mut response)?;
        exact("Probe initial upload", n, 64)?;
        // SETUP bytes a0 fe ff ea 06 00 encode a branch to 0x220284a8.
        let n = self.transport.read(0xa0, 0xfe, 0xeaff, 6, &mut response)?;
        exact("Probe response", n, 64)?;
        Ok(response)
    }

    pub(crate) fn bootrom(&mut self, mut progress: impl FnMut(usize)) -> Result<Vec<u8>> {
        let mut hash = Sha256::new();
        for (start, size) in FAST_ROM_WINDOWS {
            for offset in (start..start + size).step_by(64) {
                hash.update(
                    self.execute(&payload::memory_read(0x20000000 + offset as u32))
                        .map_err(|e| {
                            invalid(format!("BootROM preflight offset {offset:#x}: {e}"))
                        })?,
                );
            }
        }
        if format!("{:x}", hash.finalize()) != FAST_ROM_WINDOWS_SHA256 {
            return Err(invalid(
                "Unsupported BootROM fast-reply routines; stopped before fast reads or NOR access",
            ));
        }
        self.fast_echo(|_| Ok(()))?;
        let mut bytes = Vec::with_capacity(BOOTROM_SIZE);
        for offset in (0..BOOTROM_SIZE).step_by(512) {
            let mut wire = vec![0; payload::PREFIX_SIZE];
            wire.extend_from_slice(&payload::fast_bootrom(offset as u32)?);
            let response = self
                .reply(&wire, 512)
                .map_err(|e| invalid(format!("BootROM offset {offset:#x}: {e}")))?;
            bytes.extend_from_slice(&response);
            if bytes.len() % 1024 == 0 {
                progress(bytes.len());
            }
        }
        Ok(bytes)
    }

    pub(crate) fn nor_bytes(
        &mut self,
        offset: u32,
        size: usize,
        chunk_size: usize,
    ) -> Result<Vec<u8>> {
        if size == 0 || size > syscfg::MAX_SIZE || !(1..=60).contains(&chunk_size) {
            return Err(invalid("Invalid SysCfg read size"));
        }
        let mut bytes = Vec::with_capacity(size);
        while bytes.len() < size {
            let count = chunk_size.min(size - bytes.len());
            let address = offset + bytes.len() as u32;
            let response = self
                .execute(&payload::nor_read(address, count as u32)?)
                .map_err(|e| invalid(format!("NOR offset {address:#x}: {e}")))?;
            let status = u32::from_le_bytes(response[..4].try_into().unwrap());
            if status != 0 {
                return Err(invalid(format!(
                    "NOR offset {address:#x}: ROM status {status:#x}"
                )));
            }
            bytes.extend_from_slice(&response[4..4 + count]);
        }
        Ok(bytes)
    }

    // Caller must verify the full BootROM hash before these hardcoded SPI calls.
    pub(crate) fn syscfg(&mut self) -> Result<SysCfg> {
        self.execute(&payload::nor_init())?;
        let header = self.nor_bytes(0, 24, 24)?;
        let size = SysCfg::declared_size(&header)?;
        let bytes = self.nor_bytes(0, size, 60)?;
        let shifted = self.nor_bytes(1, size - 1, 59)?;
        if bytes[..24] != header || bytes[1..] != shifted {
            return Err(invalid(
                "SysCfg reads disagree across shifted/chunk boundaries",
            ));
        }
        SysCfg::parse(&bytes)
    }
}

impl<T: Transport> Dfu<T> {
    pub(crate) fn read_memory(&mut self, address: u32, size: usize) -> Result<Vec<u8>> {
        if size == 0 || size > 65536 || u64::from(address) + size as u64 > 1 << 32 {
            return Err(invalid("Invalid memory read range"));
        }
        let mut bytes = Vec::with_capacity(size);
        while bytes.len() < size {
            let n = 512.min(size - bytes.len());
            let mut wire = vec![0; payload::PREFIX_SIZE];
            wire.extend(payload::fast_memory(address + bytes.len() as u32, n)?);
            bytes.extend(self.reply(&wire, n)?);
        }
        self.rom_clean()?;
        Ok(bytes)
    }

    pub(crate) fn launch_image(&mut self, image: &[u8]) -> Result<()> {
        self.execute(&payload::image_hook())?;
        // Language zero bypasses macOS's cached pre-hook product descriptor.
        let mut descriptor = [0; 255];
        let n = self.transport.read(0x80, 6, 0x0302, 0, &mut descriptor)?;
        let expected: Vec<u8> = [20, 3]
            .into_iter()
            .chain("haxed dfu".encode_utf16().flat_map(u16::to_le_bytes))
            .collect();
        if n < expected.len() || descriptor[..expected.len()] != expected {
            return Err(invalid("IMG1 launch hook descriptor mismatch"));
        }
        self.rom_clean()?;
        self.download_image(image, |_, _| {})
    }

    pub(crate) fn download_image(
        &mut self,
        image: &[u8],
        mut emit: impl FnMut(usize, usize),
    ) -> Result<()> {
        let mut data = image.to_vec();
        data.extend(image_crc(image).to_le_bytes());
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(30);
        for (block, chunk) in data.chunks(1024).enumerate() {
            let n = self.transport.write(0x21, 1, block as u16, 0, chunk)?;
            exact("Helper image download", n, chunk.len())?;
            self.wait_image_state(false, deadline)?;
            emit(((block + 1) * 1024).min(data.len()), data.len());
        }
        let n = self
            .transport
            .write(0x21, 1, data.len().div_ceil(1024) as u16, 0, &[])?;
        exact("Helper manifest", n, 0)?;
        match self.wait_image_state(
            true,
            std::time::Instant::now() + std::time::Duration::from_secs(10),
        ) {
            // Re-enumeration and the run nonce must still prove execution.
            Err(crate::Error::Usb {
                source: rusb::Error::Timeout | rusb::Error::NoDevice,
                ..
            }) => Ok(()),
            result => result,
        }
    }

    fn wait_image_state(&mut self, manifest: bool, deadline: std::time::Instant) -> Result<()> {
        loop {
            if std::time::Instant::now() >= deadline {
                return Err(invalid("Helper launch timed out"));
            }
            let mut status = [0; 6];
            let n = self.transport.read(0xa1, 3, 0, 0, &mut status)?;
            exact("DFU GETSTATUS", n, 6)?;
            if status[0] != 0 {
                return Err(invalid(format!("DFU image error {}", status[0])));
            }
            if status[4] == if manifest { 7 } else { 5 } {
                return Ok(());
            }
            if !matches!((manifest, status[4]), (true, 6 | 4) | (false, 3 | 4)) {
                return Err(invalid(format!("Unexpected DFU image state {}", status[4])));
            }
            let millis = u32::from_le_bytes([status[1], status[2], status[3], 0]);
            std::thread::sleep(std::time::Duration::from_millis(millis.clamp(1, 100) as u64));
        }
    }
}

fn image_crc(bytes: &[u8]) -> u32 {
    let mut crc = !0u32;
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb88320 & 0u32.wrapping_sub(crc & 1));
        }
    }
    crc
}
