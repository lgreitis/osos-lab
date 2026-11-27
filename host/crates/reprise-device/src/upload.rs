// SPDX-License-Identifier: GPL-3.0-only

mod diagnostics;
mod image;
mod protocol;
mod transport;
use crate::{exact, invalid, Cleanup, DeviceInfo, Error, Result, Session};
pub use image::UploadHelper;
use protocol::{
    helper_error, more_files, next_payload, word, Status, ACKNOWLEDGE, CANCEL, COMPLETE, READY,
    RESULT_ADDR, RESULT_SIZE, START, VERIFYING,
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
            return Err(invalid(
                "Destination must be an absolute ASCII path under 192 bytes",
            ));
        }
        Ok(())
    }
}

pub trait Input: Read + Seek {}
impl<T: Read + Seek + ?Sized> Input for T {}

/// One file of a batch: its content and where it goes on the data partition.
pub struct UploadFile<'a> {
    pub input: &'a mut dyn Input,
    pub options: UploadOptions,
}

/// `file` indexes the batch. `prepare` opens each file, `launch` and `dfu-return`
/// happen once per batch.
#[derive(Clone, Debug, Serialize)]
pub struct UploadProgress {
    pub file: usize,
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
}

pub struct Uploaded {
    pub session: Session,
    pub reports: Vec<UploadReport>,
    pub cleanup: Cleanup,
}

/// `reports` covers the files of every batch whose last file was verified before
/// the failure. On FAT32, files of the failed batch may be in place without one.
#[derive(Debug, thiserror::Error, Serialize)]
#[error("{message}")]
pub struct UploadFailure {
    pub message: String,
    pub cleanup: Cleanup,
    pub reports: Vec<UploadReport>,
    /// The error behind `message`, typed so callers can tell a USB failure
    /// such as a disconnected iPod from a refused file.
    #[source]
    #[serde(skip)]
    pub error: Error,
}

impl UploadFailure {
    /// Whether the iPod stopped answering over USB during the upload.
    pub fn device_stopped(&self) -> bool {
        self.error.device_stopped()
    }
}

type Emit<'a> = dyn FnMut(UploadProgress) -> Result<()> + 'a;
/// Size and SHA-256 of one input, hashed before the helper launches.
type Hashed = (u32, [u8; 32]);

/// Older helpers end the session after the configured file.
fn check_file_count(count: usize, batch_upload: bool) -> Result<()> {
    if count == 0 {
        return Err(invalid("Nothing to upload"));
    }
    if count > 1 && !batch_upload {
        return Err(invalid(
            "This package's upload helper takes one file per launch",
        ));
    }
    Ok(())
}

/// A failed storage step followed by a failed DFU return, in the wording
/// storage inspection has always used.
fn cleanup_after(step: Error, cleanup: Error) -> Error {
    Error::Cleanup {
        step: Box::new(step),
        cleanup: Box::new(cleanup),
        advice: "",
    }
}

fn io_error(e: std::io::Error) -> Error {
    invalid(format!("Upload input: {e}"))
}

fn progress(
    emit: &mut Emit<'_>,
    file: usize,
    stage: &'static str,
    completed: u64,
    total: u64,
) -> Result<()> {
    emit(UploadProgress {
        file,
        stage,
        completed,
        total,
    })
}

fn hash_input(input: &mut dyn Input, file: usize, emit: &mut Emit<'_>) -> Result<(u64, [u8; 32])> {
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
        progress(emit, file, "hash", size - left, size)?;
    }
    let sha: [u8; 32] = hash.finalize().into();
    input.seek(SeekFrom::Start(0)).map_err(io_error)?;
    Ok((size, sha))
}

/// Streams one file and polls until the helper reports COMPLETE. The caller sends
/// ACKNOWLEDGE or NEXT afterwards.
fn transfer(
    bulk: &mut impl BulkTransport,
    input: &mut dyn Read,
    nonce: &[u8; 16],
    size: u32,
    file: usize,
    emit: &mut Emit<'_>,
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
        // Cancelling waits for the file boundary so no staged file is left behind.
        let _ = progress(emit, file, "upload", u64::from(sent), u64::from(size));
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
            return Ok(status);
        }
        let _ = progress(
            emit,
            file,
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

/// Sends every file through one helper session: the first is configured in the
/// image, each further one is queued with NEXT once the previous one is verified.
/// With `batches`, files go in batches of up to `BATCH_FILES`, all but the last
/// marked with `MORE_FILES`; an HFS+ volume publishes a batch with its last file.
/// Returns the last status after ACKNOWLEDGE; `reports` grows by a batch once its
/// last file is verified.
#[allow(clippy::too_many_arguments)]
fn transfer_files(
    bulk: &mut impl BulkTransport,
    files: &mut [UploadFile<'_>],
    hashed: &[Hashed],
    nonce: &[u8; 16],
    device: &DeviceInfo,
    batches: bool,
    reports: &mut Vec<UploadReport>,
    emit: &mut Emit<'_>,
) -> Result<Status> {
    let mut last = None;
    let mut batch = Vec::new();
    let count = files.len();
    for (index, file) in files.iter_mut().enumerate() {
        let (size, sha) = hashed[index];
        let more = batches && more_files(index, count);
        let started = Instant::now();
        // Reported before NEXT so a cancelling `emit` stops between files.
        progress(emit, index, "prepare", 0, 1)?;
        let status = (|| {
            if index > 0 {
                bulk.next(&next_payload(nonce, size, &sha, &file.options, more))?;
            }
            let status = transfer(bulk, file.input, nonce, size, index, emit)?;
            status.complete(&sha, &file.options)?;
            Ok::<_, Error>(status)
        })()
        .map_err(|e| Error::Context {
            context: file.options.destination.clone(),
            source: Box::new(e),
        })?;
        batch.push(UploadReport {
            schema_version: 2,
            device: device.clone(),
            nonce: nonce.iter().map(|b| format!("{b:02x}")).collect(),
            destination: file.options.destination.clone(),
            bytes: u64::from(size),
            sha256: sha.iter().map(|b| format!("{b:02x}")).collect(),
            overwrite: file.options.overwrite,
            seconds: started.elapsed().as_secs_f64(),
        });
        if !more {
            reports.append(&mut batch);
        }
        let _ = progress(emit, index, "complete", u64::from(size), u64::from(size));
        last = Some(status);
    }
    let last = last.ok_or_else(|| invalid("Nothing to upload"))?;
    bulk.command(ACKNOWLEDGE, nonce)?;
    Ok(last)
}

fn inspect_bulk(
    bulk: &mut impl BulkTransport,
    nonce: &[u8; 16],
    size: u32,
    directory: Option<&std::path::Path>,
    log: &mut impl FnMut(String),
) -> Result<Status> {
    let status = bulk.status(nonce, size);
    let directory = match &status {
        Ok(status) if status.state() == COMPLETE && status.rc() == 0 => None,
        _ => directory,
    };
    bulk.diagnostics(directory, log);
    let mut acknowledged = false;
    let result = (|| {
        let status = status?;
        log(status.summary());
        if status.state() != COMPLETE {
            return Err(invalid(format!(
                "Storage inspection did not complete: state={}",
                status.state()
            )));
        }
        bulk.command(ACKNOWLEDGE, nonce)?;
        acknowledged = true;
        if status.rc() != 0 {
            return Err(helper_error(status.rc(), status.error_no()));
        }
        Ok(status)
    })();
    if result.is_err() && !acknowledged {
        match bulk.command(CANCEL, nonce) {
            Ok(()) => log("Helper cancellation sent; waiting for DFU return".into()),
            Err(error) => log(format!("Helper cancellation failed: {error}")),
        }
    }
    result
}

fn log_return_record(record: &[u8; 64], log: &mut impl FnMut(String)) {
    let sectors = u64::from(word(record, 20)) | (u64::from(word(record, 24)) << 32);
    log(format!("DFU return: phase={}, storage rc={}, sector size={} bytes, sectors={sectors}, battery={} mV", word(record, 8), word(record, 12) as i32, word(record, 16), word(record, 28)));
}

/// The helper's final unmount runs after its last USB answer, so its error only
/// reaches the return record.
fn return_record_ok(record: &[u8; 64]) -> Result<()> {
    let (phase, rc) = (word(record, 8), word(record, 12) as i32);
    if phase == 4 && rc == 0 {
        return Ok(());
    }
    if phase == 4 && rc < 0 {
        return Err(invalid(format!(
            "Helper failed during DFU return: {}",
            helper_error(rc, 0)
        )));
    }
    Err(invalid(format!(
        "Helper storage return failed: phase={phase}, rc={rc}"
    )))
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
            return Err(Error::TimedOut(
                "DFU return timed out; re-enter DFU before another upload".into(),
            ));
        }
        std::thread::sleep(Duration::from_millis(100));
    }
}

impl Session {
    fn launch_helper(mut self, image: &[u8]) -> (Result<()>, bool) {
        self.operation_ready = false;
        self.dfu.transport.timeout = Duration::from_secs(2);
        let mut launched = false;
        let result = self.dfu.idle().and_then(|()| {
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
    /// Consumes checked DFU, uploads every file through one helper session and returns
    /// a newly claimed session. An error from `emit` while hashing, at launch or before
    /// a further file cancels the batch; the helper still finishes its disk cleanup and
    /// returns to DFU.
    pub fn upload_files(
        self,
        helper: &UploadHelper,
        files: &mut [UploadFile<'_>],
        mut emit: impl FnMut(UploadProgress) -> Result<()>,
        mut log: impl FnMut(String),
    ) -> std::result::Result<Uploaded, UploadFailure> {
        let mut nonce = [0; 16];
        let info = self.info.clone();
        let verified_rom = self.verified_rom.clone();
        let mut reports = Vec::new();
        let setup = (|| -> Result<(Vec<Hashed>, Vec<u8>)> {
            helper.validate_platform()?;
            check_file_count(files.len(), helper.batch_upload)?;
            if !self.operation_ready {
                return Err(invalid("Upload requires successful checks on this session"));
            }
            if info.port_path.is_empty() {
                return Err(invalid("Upload requires a stable USB port path"));
            }
            let mut hashed: Vec<Hashed> = Vec::with_capacity(files.len());
            for (index, file) in files.iter_mut().enumerate() {
                file.options.validate()?;
                let (size, sha) = hash_input(file.input, index, &mut emit)?;
                hashed.push((size as u32, sha));
            }
            getrandom::fill(&mut nonce).map_err(|e| invalid(format!("Upload nonce: {e}")))?;
            let (size, sha) = hashed[0];
            let image = helper.prepare(
                self.verified_rom
                    .as_deref()
                    .ok_or_else(|| invalid("Missing checked BootROM"))?,
                &nonce,
                size,
                &sha,
                &files[0].options,
                helper.hfs_batches && more_files(0, files.len()),
            )?;
            Ok((hashed, image))
        })();
        let (hashed, image) = match setup {
            Ok(prepared) => prepared,
            Err(e) => {
                return Err(UploadFailure {
                    message: e.to_string(),
                    cleanup: Cleanup::NotAttempted,
                    reports,
                    error: e,
                })
            }
        };
        let last = files.len() - 1;
        if let Err(e) = progress(&mut emit, 0, "launch", 0, 1) {
            return Err(UploadFailure {
                message: e.to_string(),
                cleanup: Cleanup::NotAttempted,
                reports,
                error: e,
            });
        }
        let (launch, launched) = self.launch_helper(&image);
        let transfer_result = launch.and_then(|()| {
            let mut bulk = open_bulk(&info, helper.usb, &mut log)?;
            bulk.diagnostics(None, &mut log);
            let result = transfer_files(
                &mut bulk,
                files,
                &hashed,
                &nonce,
                &info,
                helper.hfs_batches,
                &mut reports,
                &mut emit,
            );
            if result.is_err() {
                if let Err(error) = bulk.command(CANCEL, &nonce) {
                    log(format!("Helper cancellation failed: {error}"));
                }
            }
            result
        });
        if let Err(error) = &transfer_result {
            log(format!("File transfer failed: {error}"));
        }
        let _ = progress(&mut emit, last, "dfu-return", 0, 1);
        let recovered = if launched {
            returned(&info, &nonce)
        } else {
            Err(invalid("Helper did not launch"))
        };
        let (mut session, record) = match recovered {
            Ok(s) => s,
            Err(e) => {
                let cleanup = Cleanup::Failed(e.to_string());
                let (message, error) = match transfer_result {
                    Err(original) => (
                        format!("{original}; DFU cleanup failed: {e}"),
                        Error::Cleanup {
                            step: Box::new(original),
                            cleanup: Box::new(e),
                            advice: "",
                        },
                    ),
                    Ok(_) => (e.to_string(), e),
                };
                return Err(UploadFailure {
                    message,
                    cleanup,
                    reports,
                    error,
                });
            }
        };
        log_return_record(&record, &mut log);
        let validation = (|| -> Result<()> {
            let status = transfer_result?;
            return_record_ok(&record)?;
            session.verify_helper_result(&status, &nonce, hashed[last].0, "Upload")
        })();
        let idle = session.return_to_idle();
        let cleanup = match &idle {
            Ok(()) => Cleanup::Idle,
            Err(e) => Cleanup::Failed(e.to_string()),
        };
        if let Err(e) = validation {
            let message = e.to_string();
            let error = match idle {
                Ok(()) => e,
                Err(idle) => Error::Cleanup {
                    step: Box::new(e),
                    cleanup: Box::new(idle),
                    advice: "",
                },
            };
            return Err(UploadFailure {
                message,
                cleanup,
                reports,
                error,
            });
        }
        if let Err(e) = idle {
            return Err(UploadFailure {
                message: e.to_string(),
                cleanup,
                reports,
                error: e,
            });
        }
        session.verified_rom = verified_rom;
        session.operation_ready = true;
        Ok(Uploaded {
            session,
            reports,
            cleanup,
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

/// What the helper deleted, and what refused to go. `extensions` stands for the
/// `/Reprise` tree apart from the two firmware files, together with the extension
/// folders under `/Revola` and the theme packs of releases through 0.4.0. The helper
/// skips a stuck entry and carries on, so both can be true for the tree.
#[derive(Debug, Serialize)]
pub struct RemovalReport {
    pub companion: bool,
    pub firmware: bool,
    pub extensions: bool,
    pub staged_uploads: bool,
    pub companion_left: bool,
    pub firmware_left: bool,
    pub extensions_left: bool,
    pub staged_uploads_left: bool,
}

impl RemovalReport {
    /// What remains on the iPod, for the listener to delete in disk mode. The
    /// companion loader and the firmware image sit in `/Reprise/Firmware` or, for
    /// releases through 0.4.0, at the disk root.
    pub fn leftovers(&self) -> Vec<&'static str> {
        [
            (
                self.companion_left,
                "/Reprise/Firmware/loader.bin or /cfw-loader.bin",
            ),
            (
                self.firmware_left,
                "/Reprise/Firmware/reprise.bin or /osos-cfw.bin",
            ),
            (
                self.extensions_left,
                "/Reprise, /Revola/Extensions, /Revola/Data or /themes",
            ),
            (self.staged_uploads_left, "/.reprise-*.part"),
        ]
        .into_iter()
        .filter_map(|(left, path)| left.then_some(path))
        .collect()
    }
}

impl Session {
    /// Inspect the data volume, then recover the nonce-verified DFU session.
    pub fn inspect_storage(
        self,
        helper: &UploadHelper,
        required_bytes: u32,
    ) -> Result<(Self, StorageReport)> {
        self.inspect_storage_with_log(helper, required_bytes, |_| {})
    }

    pub fn inspect_storage_with_log(
        self,
        helper: &UploadHelper,
        required_bytes: u32,
        log: impl FnMut(String),
    ) -> Result<(Self, StorageReport)> {
        self.inspect_storage_with_diagnostics(helper, required_bytes, None, log)
    }

    /// Save successful sector reads in an existing directory, including failed inspections.
    /// Diagnostic failures are logged without preventing helper cleanup.
    pub fn inspect_storage_with_diagnostics(
        self,
        helper: &UploadHelper,
        required_bytes: u32,
        directory: Option<&std::path::Path>,
        log: impl FnMut(String),
    ) -> Result<(Self, StorageReport)> {
        helper.validate_platform()?;
        if !helper.storage_inspection || !self.operation_ready || self.info.port_path.is_empty() {
            return Err(invalid(
                "Storage inspection requires a current helper and checked DFU session",
            ));
        }
        self.run_storage_helper(helper, 2, required_bytes, directory, log, |data| {
            storage_report(data, required_bytes)
        })
    }

    /// Delete the RepriseOS files from the data volume, then recover the nonce-verified
    /// DFU session. Files that are already missing are not an error.
    pub fn remove_files_with_log(
        self,
        helper: &UploadHelper,
        directory: Option<&std::path::Path>,
        log: impl FnMut(String),
    ) -> Result<(Self, RemovalReport)> {
        if !helper.file_removal {
            return Err(invalid(
                "This package's upload helper cannot remove files. Choose a newer package.",
            ));
        }
        helper.validate_platform()?;
        if !self.operation_ready || self.info.port_path.is_empty() {
            return Err(invalid("File removal requires a checked DFU session"));
        }
        self.run_storage_helper(helper, 3, 0, directory, log, removal_report)
    }

    /// Run the helper in a storage mode (2 inspect, 3 remove), read its 32-byte storage
    /// result over DFU and parse it, then recover the nonce-verified session. The
    /// caller has checked the platform and the session.
    fn run_storage_helper<T>(
        mut self,
        helper: &UploadHelper,
        mode: u32,
        required_bytes: u32,
        directory: Option<&std::path::Path>,
        mut log: impl FnMut(String),
        parse: impl FnOnce(&[u8]) -> Result<T>,
    ) -> Result<(Self, T)> {
        let operation = if mode == 2 {
            "Storage inspection"
        } else {
            "File removal"
        };
        let mut nonce = [0; 16];
        getrandom::fill(&mut nonce).map_err(|e| invalid(e.to_string()))?;
        let rom = self
            .verified_rom
            .as_deref()
            .ok_or_else(|| invalid("Missing checked BootROM"))?;
        let image = helper.prepare_storage(rom, &nonce, required_bytes, mode)?;
        let info = self.info.clone();
        let verified_rom = self.verified_rom.take();
        log(if mode == 2 {
            format!("Starting storage helper; required free space: {required_bytes} bytes")
        } else {
            "Starting file removal helper".to_owned()
        });
        let (launch, _) = self.launch_helper(&image);
        let outcome = launch.and_then(|()| {
            let mut bulk = open_bulk(&info, helper.usb, &mut log)?;
            inspect_bulk(&mut bulk, &nonce, required_bytes, directory, &mut log)
        });
        if let Err(error) = &outcome {
            log(format!("{operation} failed: {error}"));
        }
        log("Waiting up to 45s for nonce-verified DFU return".into());
        let (mut session, record) = match returned(&info, &nonce) {
            Ok(returned) => returned,
            Err(cleanup) => {
                return Err(match outcome {
                    Err(step) => cleanup_after(step, cleanup),
                    Ok(_) => cleanup,
                })
            }
        };
        log_return_record(&record, &mut log);
        let result = (|| {
            if word(&record, 8) < 4 {
                let rc = word(&record, 12) as i32;
                let reason = match (word(&record, 8), rc) {
                    (1, -100) => "helper returned before storage initialization",
                    (1, -101) => "battery voltage below the helper's 3600 mV minimum",
                    _ => "disk initialization failed",
                };
                return Err(invalid(format!(
                    "Storage helper: {reason} (phase={}, code {rc}); {}",
                    word(&record, 8),
                    outcome
                        .as_ref()
                        .err()
                        .map(ToString::to_string)
                        .unwrap_or_default()
                )));
            }
            let status = outcome?;
            return_record_ok(&record)?;
            session.verify_helper_result(&status, &nonce, required_bytes, "Storage")?;
            parse(&session.dfu.read_memory(0x2201fbc0, 32)?)
        })();
        let cleanup = session.return_to_idle();
        if let Err(error) = cleanup {
            log(format!("DFU cleanup failed: {error}"));
            return Err(match result {
                Err(step) => cleanup_after(step, error),
                Ok(_) => error,
            });
        }
        log("DFU cleanup: idle".into());
        let report = result?;
        session.verified_rom = verified_rom;
        session.operation_ready = true;
        Ok((session, report))
    }
}

fn quad(data: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(data[offset..offset + 8].try_into().unwrap())
}

/// The helper's storage result after an inspection.
fn storage_report(data: &[u8], required_bytes: u32) -> Result<StorageReport> {
    let report = StorageReport {
        first_sector: quad(data, 0),
        end_sector: quad(data, 8),
        free_bytes: quad(data, 16),
        sector_bytes: word(data, 24),
        companion_exists: word(data, 28) & 1 != 0,
        osos_exists: word(data, 28) & 2 != 0,
    };
    if report.first_sector >= report.end_sector || report.free_bytes < u64::from(required_bytes) {
        return Err(invalid("Invalid storage inspection result"));
    }
    Ok(report)
}

/// The helper's storage result after a removal: bits 0-3 of the word at 28 say what
/// was deleted (companion, firmware, tree, staged uploads), bits 4-7 what remains.
pub(crate) fn removal_report(data: &[u8]) -> Result<RemovalReport> {
    let removed = word(data, 28);
    if quad(data, 0) >= quad(data, 8) || removed > 255 {
        return Err(invalid("Invalid file removal result"));
    }
    Ok(RemovalReport {
        companion: removed & 1 != 0,
        firmware: removed & 2 != 0,
        extensions: removed & 4 != 0,
        staged_uploads: removed & 8 != 0,
        companion_left: removed & 16 != 0,
        firmware_left: removed & 32 != 0,
        extensions_left: removed & 64 != 0,
        staged_uploads_left: removed & 128 != 0,
    })
}

#[cfg(test)]
mod tests;
