// SPDX-License-Identifier: GPL-3.0-only

mod image;
mod protocol;
mod transport;
use crate::{exact, invalid, Cleanup, DeviceInfo, Error, Result, Session};
pub use image::UploadHelper;
use protocol::{
    helper_error, word, Status, ACKNOWLEDGE, CANCEL, COMPLETE, READY, RESULT_ADDR, RESULT_SIZE,
    START, VERIFYING,
};
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::{
    io::{Read, Seek, SeekFrom},
    time::{Duration, Instant},
};
use transport::{open_bulk, same_port, BulkTransport};

pub const MAX_UPLOAD_SIZE: u64 = 0x7fff_ffff;
const CHUNK: usize = 65536;

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

fn hash_input(
    input: &mut (impl Read + Seek),
    emit: &mut impl FnMut(UploadProgress),
) -> Result<(u64, [u8; 32])> {
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
        progress(emit, "hash", size - left, size);
    }
    let sha: [u8; 32] = hash.finalize().into();
    input.seek(SeekFrom::Start(0)).map_err(io_error)?;
    Ok((size, sha))
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
        return Err(helper_error(ready.rc(), ready.error_no()));
    }
    if ready.state() != READY
        || ready.rc() != 0
        || ready.received() != 0
        || ready.endpoint() != u32::from(bulk.endpoint())
    {
        return Err(invalid(format!(
            "Upload helper not ready: rc={}",
            ready.rc()
        )));
    }
    bulk.command(START, nonce)?;
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
        let now = (status.state(), status.written(), status.verified());
        if now != last {
            deadline = Instant::now() + Duration::from_secs(30);
            last = now;
        }
        if status.state() == COMPLETE {
            bulk.command(ACKNOWLEDGE, nonce)?;
            return Ok(status);
        }
        progress(
            emit,
            if status.state() == VERIFYING {
                "verify"
            } else {
                "write"
            },
            u64::from(if status.state() == VERIFYING {
                now.2
            } else {
                now.1
            }),
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
    fn launch_helper(mut self, image: &[u8], before_launch: impl FnOnce()) -> (Result<()>, bool) {
        self.operation_ready = false;
        self.dfu.transport.timeout = Duration::from_secs(2);
        let mut launched = false;
        let result = self.dfu.idle().and_then(|()| {
            before_launch();
            launched = true;
            self.dfu.launch_image(image)
        });
        (result, launched)
    }

    fn verify_helper_result(
        &mut self,
        status: &Status,
        nonce: &[u8; 16],
        size: u32,
        operation: &str,
    ) -> Result<()> {
        let data = self.dfu.read_memory(RESULT_ADDR, RESULT_SIZE)?;
        if Status::parse(&data, nonce, size)? != *status {
            return Err(invalid(format!(
                "{operation} result changed during DFU return"
            )));
        }
        Ok(())
    }
}

impl Session {
    /// Consumes checked DFU and returns a newly claimed session after the upload.
    /// Progress callbacks report activity; cancellation must not interrupt disk cleanup.
    pub fn upload(
        self,
        helper: &UploadHelper,
        input: &mut (impl Read + Seek),
        options: UploadOptions,
        mut emit: impl FnMut(UploadProgress),
    ) -> std::result::Result<Uploaded, UploadFailure> {
        let started = Instant::now();
        let mut nonce = [0; 16];
        let info = self.info.clone();
        let verified_rom = self.verified_rom.clone();
        let setup = (|| -> Result<(u32, [u8; 32], Vec<u8>)> {
            helper.validate_platform()?;
            options.validate()?;
            if !self.operation_ready {
                return Err(invalid("Upload requires successful checks on this session"));
            }
            if info.port_path.is_empty() {
                return Err(invalid("Upload requires a stable USB port path"));
            }
            let (size, sha) = hash_input(input, &mut emit)?;
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
        let (launch, launched) = self.launch_helper(&image, || progress(&mut emit, "launch", 0, 1));
        let transfer_result = launch.and_then(|()| {
            let mut bulk = open_bulk(&info, helper.usb)?;
            let result = transfer(&mut bulk, input, &nonce, size, &mut emit);
            if result.is_err() {
                let _ = bulk.command(CANCEL, &nonce);
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
            session.verify_helper_result(&status, &nonce, size, "Upload")?;
            status.complete(&sha, &options)
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
        helper.validate_platform()?;
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
        let image = helper.prepare_inspection(rom, &nonce, required_bytes)?;
        let info = self.info.clone();
        let verified_rom = self.verified_rom.take();
        let (launch, _) = self.launch_helper(&image, || {});
        let operation = launch.and_then(|()| {
            let mut bulk = open_bulk(&info, helper.usb)?;
            let status = bulk.status(&nonce, required_bytes)?;
            if status.state() != COMPLETE {
                let _ = bulk.command(CANCEL, &nonce);
                return Err(invalid("Storage inspection did not complete"));
            }
            bulk.command(ACKNOWLEDGE, &nonce)?;
            if status.rc() != 0 {
                return Err(helper_error(status.rc(), status.error_no()));
            }
            Ok(status)
        });
        let (mut session, record) = returned(&info, &nonce).map_err(|error| match &operation {
            Err(original) => invalid(format!("{original}; {error}")),
            Ok(_) => error,
        })?;
        let result = (|| {
            let status = operation?;
            if word(&record, 8) != 4 || word(&record, 12) != 0 {
                return Err(invalid("Storage helper failed during DFU return"));
            }
            session.verify_helper_result(&status, &nonce, required_bytes, "Storage")?;
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
