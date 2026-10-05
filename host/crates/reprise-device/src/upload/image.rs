// SPDX-License-Identifier: GPL-3.0-only
use super::{protocol::word, UploadOptions};
use crate::{exact, invalid, targets, Result};
use serde::Deserialize;
use sha2::{Digest, Sha256};

pub(super) const COLD_INIT_SIZE: usize = 1024;
pub(super) const COLD_INIT_TAG: &[u8] = b"\xfe\xff\xff\xeaREPRISE-ROM\0";
pub(super) const ROM_START: usize = 0xc0;
pub(super) const ROM_END: usize = 0x284;

#[derive(Deserialize)]
struct Manifest {
    schema: u32,
    mode: String,
    rom_sha256: String,
    bytes: usize,
    sha256: String,
    nonce_offset: usize,
    config_offset: usize,
    cold_init_offset: usize,
    #[serde(default)]
    storage_inspection: bool,
    #[serde(default)]
    usb: UsbIdentity,
}

#[derive(Clone, Copy, Deserialize)]
pub(super) struct UsbIdentity {
    pub(super) vendor_id: u16,
    pub(super) product_id: u16,
    pub(super) winusb: bool,
}

impl Default for UsbIdentity {
    fn default() -> Self {
        Self {
            vendor_id: 0x05ac,
            product_id: 0x1261,
            winusb: false,
        }
    }
}

struct Config<'a> {
    size: u32,
    // Wire field `overwrite`: 0 create, 1 replace, 2 inspect storage.
    mode: u32,
    sha: &'a [u8; 32],
    destination: &'a str,
}

pub struct UploadHelper {
    bootrom_sha256: String,
    pub(super) image: Vec<u8>,
    pub(super) nonce_offset: usize,
    pub(super) config_offset: usize,
    pub(super) cold_init_offset: usize,
    pub(super) storage_inspection: bool,
    pub(super) usb: UsbIdentity,
}

impl UploadHelper {
    /// Load an image and its build manifest before opening USB.
    pub fn from_bytes(image: &[u8], manifest: &[u8]) -> Result<Self> {
        let m: Manifest = serde_json::from_slice(manifest)
            .map_err(|e| invalid(format!("Helper manifest: {e}")))?;
        if m.usb.vendor_id == 0
            || m.usb.product_id == 0
            || (m.usb.winusb && m.usb.vendor_id == 0x05ac)
        {
            return Err(invalid("Invalid upload helper USB identity"));
        }
        if m.schema != 3
            || m.mode != "stream-file"
            || !targets::supports_bootrom(&m.rom_sha256)
            || !(0x810..=0x1eff0).contains(&image.len())
            || !image.len().is_multiple_of(16)
            || m.bytes != image.len()
            || m.sha256 != format!("{:x}", Sha256::digest(image))
            || &image[..8] != b"87021.0\x02"
            || word(image, 8) != 0
            || word(image, 12) as usize != image.len() - 0x800
        {
            return Err(invalid("Helper image/manifest mismatch"));
        }
        slot(image, m.nonce_offset, b"REPRISE-NONCE-01", 16)?;
        slot(image, m.config_offset, b"REPRISE-UPLOAD2\0", 248)?;
        slot(image, m.cold_init_offset, COLD_INIT_TAG, COLD_INIT_SIZE)?;
        let mut slots = [
            (m.nonce_offset, 16),
            (m.config_offset, 248),
            (m.cold_init_offset, COLD_INIT_SIZE),
        ];
        slots.sort_unstable();
        if !m.cold_init_offset.is_multiple_of(4)
            || slots.windows(2).any(|s| s[0].0 + s[0].1 > s[1].0)
        {
            return Err(invalid("Misaligned or overlapping helper slots"));
        }
        Ok(Self {
            bootrom_sha256: m.rom_sha256,
            image: image.to_vec(),
            nonce_offset: m.nonce_offset,
            config_offset: m.config_offset,
            cold_init_offset: m.cold_init_offset,
            storage_inspection: m.storage_inspection,
            usb: m.usb,
        })
    }

    pub fn bootrom_sha256(&self) -> &str {
        &self.bootrom_sha256
    }

    pub fn supports_storage_inspection(&self) -> bool {
        self.storage_inspection
    }

    pub fn validate_platform(&self) -> Result<()> {
        if cfg!(windows) && !self.usb.winusb {
            return Err(invalid("This bundle's upload helper lacks Windows WinUSB support. Select a rebuilt firmware bundle."));
        }
        Ok(())
    }

    pub(super) fn prepare(
        &self,
        rom: &[u8],
        nonce: &[u8; 16],
        size: u32,
        sha: &[u8; 32],
        options: &UploadOptions,
    ) -> Result<Vec<u8>> {
        options.validate()?;
        self.prepare_config(
            rom,
            nonce,
            Config {
                size,
                sha,
                destination: &options.destination,
                mode: u32::from(options.overwrite),
            },
        )
    }

    pub(super) fn prepare_inspection(
        &self,
        rom: &[u8],
        nonce: &[u8; 16],
        size: u32,
    ) -> Result<Vec<u8>> {
        self.prepare_config(
            rom,
            nonce,
            Config {
                size,
                sha: &[0; 32],
                destination: "/cfw-loader.bin",
                mode: 2,
            },
        )
    }

    fn prepare_config(&self, rom: &[u8], nonce: &[u8; 16], config: Config<'_>) -> Result<Vec<u8>> {
        exact("BootROM image", rom.len(), crate::BOOTROM_SIZE)?;
        if format!("{:x}", Sha256::digest(rom)) != self.bootrom_sha256 {
            return Err(invalid("Unsupported BootROM for upload helper"));
        }
        let cold_init = relocate_cold_init(rom)?;
        let mut image = self.image.clone();
        image[self.cold_init_offset..self.cold_init_offset + COLD_INIT_SIZE]
            .copy_from_slice(&cold_init);
        image[self.nonce_offset..self.nonce_offset + 16].copy_from_slice(nonce);
        let Config {
            size,
            mode,
            sha,
            destination,
        } = config;
        let config = &mut image[self.config_offset..self.config_offset + 248];
        config[16..20].copy_from_slice(&size.to_le_bytes());
        config[20..24].copy_from_slice(&mode.to_le_bytes());
        config[24..56].copy_from_slice(sha);
        config[56..56 + destination.len()].copy_from_slice(destination.as_bytes());
        Ok(image)
    }
}

// Keep instruction positions for internal branches; place literal data after a jump.
pub(super) fn relocate_cold_init(rom: &[u8]) -> Result<[u8; COLD_INIT_SIZE]> {
    exact("BootROM image", rom.len(), crate::BOOTROM_SIZE)?;
    let code_size = ROM_END - ROM_START;
    let mut out = [0; COLD_INIT_SIZE];
    let mut pool = code_size + 4;
    for off in (ROM_START..ROM_END).step_by(4) {
        let mut instruction = word(rom, off);
        let pos = off - ROM_START;
        if instruction & 0xffff0000 == 0xe59f0000 {
            let source = off + 8 + (instruction & 0xfff) as usize;
            if source + 4 > rom.len() || pool + 4 > out.len() {
                return Err(invalid("BootROM literal outside relocation bounds"));
            }
            out[pool..pool + 4].copy_from_slice(&rom[source..source + 4]);
            instruction = (instruction & !0xfff) | (pool - pos - 8) as u32;
            pool += 4;
        } else if instruction & 0x0e000000 == 0x0a000000 {
            let delta = ((instruction << 8) as i32) >> 6;
            let target = off as i32 + 8 + delta;
            if !(ROM_START as i32..=ROM_END as i32).contains(&target) {
                return Err(invalid("BootROM branch outside reset sequence"));
            }
        }
        out[pos..pos + 4].copy_from_slice(&instruction.to_le_bytes());
    }
    let skip_pool = 0xea000000 | ((COLD_INIT_SIZE - code_size - 8) / 4) as u32;
    out[code_size..code_size + 4].copy_from_slice(&skip_pool.to_le_bytes());
    Ok(out)
}

fn slot(image: &[u8], offset: usize, tag: &[u8], size: usize) -> Result<()> {
    if offset < 0x800
        || offset.checked_add(size).is_none_or(|end| end > image.len())
        || image.windows(tag.len()).filter(|w| *w == tag).count() != 1
        || &image[offset..offset + tag.len()] != tag
        || image[offset + tag.len()..offset + size]
            .iter()
            .any(|b| *b != 0)
    {
        return Err(invalid("Invalid helper patch slot"));
    }
    Ok(())
}
