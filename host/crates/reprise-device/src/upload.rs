// SPDX-License-Identifier: GPL-3.0-only

use crate::{
    exact, invalid, usb::usb_error, Cleanup, DeviceInfo, Error, Result, Session,
    SUPPORTED_BOOTROM_SHA256,
};
use rusb::{Context, DeviceHandle, UsbContext};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    io::{Read, Seek, SeekFrom},
    time::{Duration, Instant},
};

pub const MAX_UPLOAD_SIZE: u64 = 0x7fff_ffff;
const CHUNK: usize = 65536;
const COLD_INIT_SIZE: usize = 1024;
const COLD_INIT_TAG: &[u8] = b"\xfe\xff\xff\xeaREPRISE-ROM\0";
const ROM_START: usize = 0xc0;
const ROM_END: usize = 0x284;
const RESULT_ADDR: u32 = 0x2201fa80;
const RESULT_SIZE: usize = 320;
const TIMEOUT: Duration = Duration::from_secs(15);

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
}

pub struct UploadHelper {
    image: Vec<u8>,
    nonce_offset: usize,
    config_offset: usize,
    cold_init_offset: usize,
    storage_inspection: bool,
}

impl UploadHelper {
    /// Load an image and its build manifest before opening USB.
    pub fn from_bytes(image: &[u8], manifest: &[u8]) -> Result<Self> {
        let m: Manifest = serde_json::from_slice(manifest)
            .map_err(|e| invalid(format!("Helper manifest: {e}")))?;
        if m.schema != 3
            || m.mode != "stream-file"
            || m.rom_sha256 != SUPPORTED_BOOTROM_SHA256
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
            image: image.to_vec(),
            nonce_offset: m.nonce_offset,
            config_offset: m.config_offset,
            cold_init_offset: m.cold_init_offset,
            storage_inspection: m.storage_inspection,
        })
    }

    pub fn supports_storage_inspection(&self) -> bool {
        self.storage_inspection
    }

    fn prepare(
        &self,
        rom: &[u8],
        nonce: &[u8; 16],
        size: u32,
        sha: &[u8; 32],
        options: &UploadOptions,
    ) -> Result<Vec<u8>> {
        exact("BootROM image", rom.len(), crate::BOOTROM_SIZE)?;
        if format!("{:x}", Sha256::digest(rom)) != SUPPORTED_BOOTROM_SHA256 {
            return Err(invalid("Unsupported BootROM for upload helper"));
        }
        let cold_init = relocate_cold_init(rom)?;
        let mut image = self.image.clone();
        image[self.cold_init_offset..self.cold_init_offset + COLD_INIT_SIZE]
            .copy_from_slice(&cold_init);
        image[self.nonce_offset..self.nonce_offset + 16].copy_from_slice(nonce);
        let config = &mut image[self.config_offset..self.config_offset + 248];
        config[16..20].copy_from_slice(&size.to_le_bytes());
        config[20..24].copy_from_slice(&u32::from(options.overwrite).to_le_bytes());
        config[24..56].copy_from_slice(sha);
        config[56..56 + options.destination.len()].copy_from_slice(options.destination.as_bytes());
        Ok(image)
    }
}

// Keep instruction positions for internal branches; place literal data after a jump.
fn relocate_cold_init(rom: &[u8]) -> Result<[u8; COLD_INIT_SIZE]> {
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

#[derive(Clone, Debug)]
pub struct UploadOptions {
    pub destination: String,
    pub overwrite: bool,
}

impl UploadOptions {
    pub fn validate(&self) -> Result<()> {
        let p = &self.destination;
        if p.len() < 2
            || p.len() >= 192
            || !p.starts_with('/')
            || p.to_ascii_lowercase().starts_with("/.reprise-")
            || !p
                .bytes()
                .all(|c| (32..=126).contains(&c) && !b"\\:*?\"<>|".contains(&c))
            || p[1..]
                .split('/')
                .any(|c| c.is_empty() || c == "." || c == ".." || c.ends_with(['.', ' ']))
        {
            return Err(invalid("Destination must be an absolute ASCII FAT path under 192 bytes; use existing directories"));
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct UploadProgress {
    pub stage: &'static str,
    pub completed: u64,
    pub total: u64,
}

#[derive(Debug, Serialize)]
pub struct UploadReport {
    pub schema_version: u32,
    pub device: DeviceInfo,
    pub nonce: String,
    pub destination: String,
    pub bytes: u64,
    pub sha256: String,
    pub overwrite: bool,
    pub seconds: f64,
    pub cleanup: Cleanup,
}

pub struct Uploaded {
    pub session: Session,
    pub report: UploadReport,
}

#[derive(Debug, thiserror::Error, Serialize)]
#[error("{message}")]
pub struct UploadFailure {
    pub message: String,
    pub cleanup: Cleanup,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct Status {
    raw: Vec<u8>,
}

impl Status {
    fn parse(raw: &[u8], nonce: &[u8; 16], size: u32) -> Result<Self> {
        exact("Upload status", raw.len(), RESULT_SIZE)?;
        if word(raw, 0) != 0x55504c32
            || word(raw, 4) != 2
            || &raw[8..24] != nonce
            || word(raw, 32) != size
            || word(raw, 36) > size
            || word(raw, 40) > word(raw, 36)
            || word(raw, 44) > size
            || !(1..=4).contains(&word(raw, 24))
            || word(raw, 48) == 0
            || word(raw, 48) > 15
            || word(raw, 52) > 1
        {
            return Err(invalid("Upload session identity/progress mismatch"));
        }
        Ok(Self { raw: raw.to_vec() })
    }

    fn state(&self) -> u32 {
        word(&self.raw, 24)
    }

    fn rc(&self) -> i32 {
        word(&self.raw, 28) as i32
    }

    fn complete(&self, sha: &[u8; 32], options: &UploadOptions) -> Result<()> {
        let b = &self.raw;
        if self.rc() != 0 {
            return Err(helper_error(self.rc(), word(b, 56)));
        }
        if self.state() != 4
            || self.rc() != 0
            || word(b, 56) != 0
            || word(b, 60) != 0
            || [36, 40, 44].iter().any(|at| word(b, *at) != word(b, 32))
            || &b[64..96] != sha
            || &b[96..128] != sha
        {
            return Err(invalid(format!(
                "Upload verification failed: state={}, rc={}, errno={}, written={}, verified={}",
                self.state(),
                self.rc(),
                word(b, 56),
                word(b, 40),
                word(b, 44)
            )));
        }
        let mut path = [0u8; 192];
        path[..options.destination.len()].copy_from_slice(options.destination.as_bytes());
        if b[128..] != path {
            return Err(invalid("Upload destination mismatch"));
        }
        Ok(())
    }
}

fn helper_error(rc: i32, errno: u32) -> Error {
    let reason = match rc {
        -301 | -303 => "Disk layout does not match the supported data partition",
        -302 => "Invalid upload path or size",
        -304 => "Destination exists; use --overwrite to replace it",
        -305 => "Uploaded content differs from the input hash",
        -306 => "Failed to flush the temporary file",
        -307 | -315 | -320 => "Failed to close the file",
        -308 | -321 => "Failed to mount or unmount the data partition",
        -309 => "Temporary file could not be reopened with the expected size",
        -310 => "Temporary file could not be created",
        -311 | -312 => "Failed to write the temporary file",
        -313 | -314 => "Disk readback verification failed",
        -322 => "Not enough free space on the data partition",
        -316 => "Failed to replace the destination",
        -411 => "USB disconnected during upload",
        -412 => "Incomplete USB transfer",
        -415 => "Upload helper timed out",
        -416 => "Upload aborted",
        _ => "Upload helper failed",
    };
    invalid(format!("{reason} (code {rc}, errno {errno})"))
}

fn word(b: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(b[at..at + 4].try_into().unwrap())
}

fn io_error(e: std::io::Error) -> Error {
    invalid(format!("Upload input: {e}"))
}

fn progress(
    emit: &mut impl FnMut(UploadProgress),
    stage: &'static str,
    completed: u64,
    total: u64,
) {
    emit(UploadProgress {
        stage,
        completed,
        total,
    });
}

struct Bulk {
    handle: DeviceHandle<Context>,
    endpoint: u8,
}
trait BulkTransport {
    fn endpoint(&self) -> u8;
    fn status(&mut self, nonce: &[u8; 16], size: u32) -> Result<Status>;
    fn command(&mut self, request: u8, nonce: &[u8; 16]) -> Result<()>;
    fn send(&mut self, data: &[u8]) -> Result<usize>;
}

impl BulkTransport for Bulk {
    fn endpoint(&self) -> u8 {
        self.endpoint
    }

    fn send(&mut self, data: &[u8]) -> Result<usize> {
        self.handle
            .write_bulk(self.endpoint, data, TIMEOUT)
            .map_err(|e| usb_error("file upload", e))
    }

    fn status(&mut self, nonce: &[u8; 16], size: u32) -> Result<Status> {
        let mut b = [0; RESULT_SIZE];
        let n = self
            .handle
            .read_control(0xa1, 0x52, 0, 0, &mut b, TIMEOUT)
            .map_err(|e| usb_error("upload status", e))?;
        Status::parse(&b[..n], nonce, size)
    }

    fn command(&mut self, request: u8, nonce: &[u8; 16]) -> Result<()> {
        let n = self
            .handle
            .write_control(0x21, request, 0, 0, nonce, TIMEOUT)
            .map_err(|e| usb_error("upload command", e))?;
        exact("Upload command", n, 16)
    }
}

fn same_port(a: &DeviceInfo, bus: u8, ports: &[u8]) -> bool {
    a.selector.bus == bus && a.port_path == ports && !ports.is_empty()
}

fn open_bulk(info: &DeviceInfo) -> Result<Bulk> {
    let context = Context::new().map_err(|e| usb_error("upload context", e))?;
    let deadline = Instant::now() + Duration::from_secs(45);
    loop {
        for device in context
            .devices()
            .map_err(|e| usb_error("upload enumeration", e))?
            .iter()
        {
            if !same_port(
                info,
                device.bus_number(),
                &device
                    .port_numbers()
                    .map_err(|e| usb_error("upload port", e))?,
            ) {
                continue;
            }
            let desc = device
                .device_descriptor()
                .map_err(|e| usb_error("upload descriptor", e))?;
            if desc.vendor_id() != 0x05ac || desc.product_id() != 0x1261 {
                continue;
            }
            let config = device
                .active_config_descriptor()
                .map_err(|e| usb_error("upload configuration", e))?;
            let interface = config
                .interfaces()
                .flat_map(|i| i.descriptors())
                .find(|d| {
                    d.interface_number() == 0
                        && d.setting_number() == 0
                        && d.class_code() == 0xff
                        && d.sub_class_code() == 0x52
                        && d.protocol_code() == 2
                        && d.num_endpoints() == 1
                })
                .ok_or_else(|| invalid("Unexpected upload USB interface"))?;
            let ep = interface.endpoint_descriptors().next().unwrap();
            if ep.direction() != rusb::Direction::Out
                || ep.transfer_type() != rusb::TransferType::Bulk
            {
                return Err(invalid("Unexpected upload endpoint"));
            }
            let handle = device.open().map_err(|e| usb_error("upload open", e))?;
            handle
                .claim_interface(0)
                .map_err(|e| usb_error("upload claim", e))?;
            return Ok(Bulk {
                handle,
                endpoint: ep.address(),
            });
        }
        if Instant::now() >= deadline {
            return Err(invalid("Upload helper did not enumerate"));
        }
        std::thread::sleep(Duration::from_millis(100));
    }
}

fn transfer(
    bulk: &mut impl BulkTransport,
    input: &mut impl Read,
    nonce: &[u8; 16],
    size: u32,
    emit: &mut impl FnMut(UploadProgress),
) -> Result<Status> {
    let ready = bulk.status(nonce, size)?;
    if ready.rc() != 0 {
        return Err(helper_error(ready.rc(), word(&ready.raw, 56)));
    }
    if ready.state() != 1
        || ready.rc() != 0
        || word(&ready.raw, 36) != 0
        || word(&ready.raw, 48) != u32::from(bulk.endpoint())
    {
        return Err(invalid(format!(
            "Upload helper not ready: rc={}",
            ready.rc()
        )));
    }
    bulk.command(0x53, nonce)?;
    let mut buffer = [0; CHUNK];
    let mut sent = 0u32;
    while sent < size {
        let count = CHUNK.min((size - sent) as usize);
        let wire_size = count.div_ceil(512) * 512;
        input.read_exact(&mut buffer[..count]).map_err(io_error)?;
        buffer[count..wire_size].fill(0);
        let n = bulk.send(&buffer[..wire_size])?;
        exact("File upload", n, wire_size)?;
        sent += count as u32;
        progress(emit, "upload", u64::from(sent), u64::from(size));
    }

    // Re-hashing on the device catches changes to the input after preflight.
    let mut extra = [0];
    if input.read(&mut extra).map_err(io_error)? != 0 {
        return Err(invalid("Upload input grew during transfer"));
    }
    let mut last = (0, 0, 0);
    let mut deadline = Instant::now() + Duration::from_secs(30);
    loop {
        let status = bulk.status(nonce, size)?;
        let now = (status.state(), word(&status.raw, 40), word(&status.raw, 44));
        if now != last {
            deadline = Instant::now() + Duration::from_secs(30);
            last = now;
        }
        if status.state() == 4 {
            bulk.command(0x54, nonce)?;
            return Ok(status);
        }
        progress(
            emit,
            if status.state() == 3 {
                "verify"
            } else {
                "write"
            },
            u64::from(if status.state() == 3 { now.2 } else { now.1 }),
            u64::from(size),
        );
        if Instant::now() >= deadline {
            return Err(invalid("Upload progress timed out"));
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

fn returned(info: &DeviceInfo, nonce: &[u8; 16]) -> Result<(Session, [u8; 64])> {
    let deadline = Instant::now() + Duration::from_secs(45);
    loop {
        for candidate in crate::discover()? {
            if !candidate.dfu_candidate
                || !same_port(info, candidate.selector.bus, &candidate.port_path)
            {
                continue;
            }
            let mut session = Session::open(Some(candidate.selector))?;
            session.dfu.transport.timeout = Duration::from_secs(2);
            if session.dfu.state()? != 2 {
                return Err(invalid("Returned device is not DFU idle"));
            }
            let record = session
                .dfu
                .execute(&crate::payload::memory_read(0x2201f000))?;
            if word(&record, 0) != 0x44505231
                || word(&record, 4) != 1
                || &record[32..48] != nonce
                || word(&record, 48) != 1
            {
                // The original enumeration may still be visible just after launch.
                session.dfu.idle()?;
            } else {
                session.dfu.idle()?;
                return Ok((session, record));
            }
        }
        if Instant::now() >= deadline {
            return Err(invalid(
                "DFU return timed out; re-enter DFU before another upload",
            ));
        }
        std::thread::sleep(Duration::from_millis(100));
    }
}

impl Session {
    /// Consumes checked DFU and returns a newly claimed session after the upload.
    /// Progress callbacks report activity; cancellation must not interrupt disk cleanup.
    pub fn upload(
        mut self,
        helper: &UploadHelper,
        input: &mut (impl Read + Seek),
        options: UploadOptions,
        mut emit: impl FnMut(UploadProgress),
    ) -> std::result::Result<Uploaded, UploadFailure> {
        let started = Instant::now();
        let mut launched = false;
        let mut nonce = [0; 16];
        let info = self.info.clone();
        let verified_rom = self.verified_rom.clone();
        let setup = (|| -> Result<(u32, [u8; 32], Vec<u8>)> {
            options.validate()?;
            if !self.operation_ready {
                return Err(invalid("Upload requires successful checks on this session"));
            }
            if info.port_path.is_empty() {
                return Err(invalid("Upload requires a stable USB port path"));
            }
            let size = input.seek(SeekFrom::End(0)).map_err(io_error)?;
            if size > MAX_UPLOAD_SIZE {
                return Err(invalid(
                    "Upload exceeds the 2 GiB minus 1 byte filesystem limit",
                ));
            }
            input.seek(SeekFrom::Start(0)).map_err(io_error)?;
            let mut hash = Sha256::new();
            let mut buffer = [0; CHUNK];
            let mut left = size;
            while left > 0 {
                let n = CHUNK.min(left as usize);
                input.read_exact(&mut buffer[..n]).map_err(io_error)?;
                hash.update(&buffer[..n]);
                left -= n as u64;
                progress(&mut emit, "hash", size - left, size);
            }
            let sha: [u8; 32] = hash.finalize().into();
            input.seek(SeekFrom::Start(0)).map_err(io_error)?;
            getrandom::fill(&mut nonce).map_err(|e| invalid(format!("Upload nonce: {e}")))?;
            Ok((
                size as u32,
                sha,
                helper.prepare(
                    self.verified_rom
                        .as_deref()
                        .ok_or_else(|| invalid("Missing checked BootROM"))?,
                    &nonce,
                    size as u32,
                    &sha,
                    &options,
                )?,
            ))
        })();
        let (size, sha, image) = setup.map_err(|e| UploadFailure {
            message: e.to_string(),
            cleanup: Cleanup::NotAttempted,
        })?;
        self.operation_ready = false;
        self.dfu.transport.timeout = Duration::from_secs(2);
        let launch = (|| {
            self.dfu.idle()?;
            progress(&mut emit, "launch", 0, 1);
            launched = true;
            self.dfu.launch_image(&image)
        })();
        drop(self);
        let transfer_result = launch.and_then(|()| {
            let mut bulk = open_bulk(&info)?;
            let result = transfer(&mut bulk, input, &nonce, size, &mut emit);
            if result.is_err() {
                let _ = bulk.command(0x55, &nonce);
            }
            result
        });
        progress(&mut emit, "dfu-return", 0, 1);
        let recovered = if launched {
            returned(&info, &nonce)
        } else {
            Err(invalid("Helper did not launch"))
        };
        let (mut session, record) = match recovered {
            Ok(s) => s,
            Err(e) => {
                return Err(UploadFailure {
                    message: match transfer_result {
                        Err(original) => format!("{original}; {e}"),
                        Ok(_) => e.to_string(),
                    },
                    cleanup: Cleanup::Failed(e.to_string()),
                })
            }
        };
        let validation = (|| -> Result<()> {
            let status = transfer_result?;
            if word(&record, 8) != 4 || word(&record, 12) != 0 {
                return Err(invalid(format!(
                    "Helper storage return: phase={}, rc={}",
                    word(&record, 8),
                    word(&record, 12) as i32
                )));
            }
            let data = session.dfu.read_memory(RESULT_ADDR, RESULT_SIZE)?;
            let preserved = Status::parse(&data, &nonce, size)?;
            if preserved != status {
                return Err(invalid("Upload result changed during DFU return"));
            }
            preserved.complete(&sha, &options)
        })();
        let cleanup = match session.return_to_idle() {
            Ok(()) => Cleanup::Idle,
            Err(e) => Cleanup::Failed(e.to_string()),
        };
        if let Err(e) = validation {
            return Err(UploadFailure {
                message: e.to_string(),
                cleanup,
            });
        }
        if let Cleanup::Failed(ref e) = cleanup {
            return Err(UploadFailure {
                message: e.clone(),
                cleanup,
            });
        }
        session.verified_rom = verified_rom;
        session.operation_ready = true;
        progress(&mut emit, "complete", u64::from(size), u64::from(size));
        Ok(Uploaded {
            report: UploadReport {
                schema_version: 1,
                device: session.info.clone(),
                nonce: nonce.iter().map(|b| format!("{b:02x}")).collect(),
                destination: options.destination,
                bytes: u64::from(size),
                sha256: sha.iter().map(|b| format!("{b:02x}")).collect(),
                overwrite: options.overwrite,
                seconds: started.elapsed().as_secs_f64(),
                cleanup,
            },
            session,
        })
    }
}

#[derive(Debug, Serialize)]
pub struct StorageReport {
    pub first_sector: u64,
    pub end_sector: u64,
    pub free_bytes: u64,
    pub sector_bytes: u32,
    pub companion_exists: bool,
    pub osos_exists: bool,
}

impl Session {
    /// Inspect the data volume, then recover the nonce-verified DFU session.
    pub fn inspect_storage(
        mut self,
        helper: &UploadHelper,
        required_bytes: u32,
    ) -> Result<(Self, StorageReport)> {
        if !helper.storage_inspection || !self.operation_ready || self.info.port_path.is_empty() {
            return Err(invalid(
                "Storage inspection requires a current helper and checked DFU session",
            ));
        }
        let mut nonce = [0; 16];
        getrandom::fill(&mut nonce).map_err(|e| invalid(e.to_string()))?;
        let rom = self
            .verified_rom
            .as_deref()
            .ok_or_else(|| invalid("Missing checked BootROM"))?;
        let mut image = helper.prepare(
            rom,
            &nonce,
            required_bytes,
            &[0; 32],
            &UploadOptions {
                destination: "/cfw-loader.bin".into(),
                overwrite: true,
            },
        )?;
        image[helper.config_offset + 20..helper.config_offset + 24]
            .copy_from_slice(&2u32.to_le_bytes());
        let info = self.info.clone();
        let verified_rom = self.verified_rom.take();
        self.operation_ready = false;
        self.dfu.transport.timeout = Duration::from_secs(2);
        let launch = self.dfu.idle().and_then(|()| self.dfu.launch_image(&image));
        drop(self);
        let operation = launch.and_then(|()| {
            let mut bulk = open_bulk(&info)?;
            let status = bulk.status(&nonce, required_bytes)?;
            if status.state() != 4 {
                let _ = bulk.command(0x55, &nonce);
                return Err(invalid("Storage inspection did not complete"));
            }
            bulk.command(0x54, &nonce)?;
            Ok(status)
        });
        let (mut session, record) = returned(&info, &nonce)?;
        let result = (|| {
            let status = operation?;
            if status.rc() != 0 {
                return Err(helper_error(status.rc(), word(&status.raw, 56)));
            }
            if word(&record, 8) != 4 || word(&record, 12) != 0 {
                return Err(invalid("Storage helper failed during DFU return"));
            }
            let preserved = session.dfu.read_memory(RESULT_ADDR, RESULT_SIZE)?;
            if Status::parse(&preserved, &nonce, required_bytes)? != status {
                return Err(invalid("Storage result changed during DFU return"));
            }
            let data = session.dfu.read_memory(0x2201fbc0, 32)?;
            let quad = |offset| u64::from_le_bytes(data[offset..offset + 8].try_into().unwrap());
            let report = StorageReport {
                first_sector: quad(0),
                end_sector: quad(8),
                free_bytes: quad(16),
                sector_bytes: word(&data, 24),
                companion_exists: word(&data, 28) & 1 != 0,
                osos_exists: word(&data, 28) & 2 != 0,
            };
            if report.first_sector >= report.end_sector
                || report.free_bytes < u64::from(required_bytes)
            {
                return Err(invalid("Invalid storage inspection result"));
            }
            Ok(report)
        })();
        session.return_to_idle()?;
        let report = result?;
        session.verified_rom = verified_rom;
        session.operation_ready = true;
        Ok((session, report))
    }
}

#[cfg(test)]
mod tests;
