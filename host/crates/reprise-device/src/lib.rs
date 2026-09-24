// SPDX-License-Identifier: GPL-3.0-only

//! Classic DFU checks, NOR reads, AES decryption, and verified file uploads.

mod decrypt;
mod dfu;
mod img1;
mod install;
mod nor;
mod nor_image;
mod payload;
mod syscfg;

#[cfg(test)]
mod tests;
mod upload;
mod usb;
pub use upload::{
    StorageReport, UploadFailure, UploadHelper, UploadOptions, UploadProgress, UploadReport,
    Uploaded, MAX_UPLOAD_SIZE,
};

pub use decrypt::{DecryptOptions, DecryptProgress, DecryptReport, Decrypted};
pub use img1::classic_img1_body_range;
pub use nor::{NorDump, NorProgress, NorReport, NOR_SIZE};
pub use nor_image::{AppleNorImage, APPLE_LOADER_BYTES, APPLE_LOADER_SHA256};
pub use syscfg::{Identity, SysCfg};
pub use usb::{discover, DeviceInfo, DeviceSelector, Session};

use serde::Serialize;
use sha2::{Digest, Sha256};

pub const BOOTROM_SIZE: usize = 0x10000;
pub const SUPPORTED_BOOTROM_SHA256: &str =
    "69c087afc5753d7f0f11f09b141b372af753a854bc526673ea444c486d6003e4";

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("Expected one matching BootROM DFU device, found {0}")]
    DeviceCount(usize),
    #[error("USB {operation}: {source}")]
    Usb {
        operation: &'static str,
        source: rusb::Error,
    },
    #[error("{0}")]
    Invalid(String),
    #[error("{operation}: received {actual} bytes, expected {expected}")]
    ShortTransfer {
        operation: &'static str,
        actual: usize,
        expected: usize,
    },
}

pub type Result<T> = std::result::Result<T, Error>;

fn invalid(message: impl Into<String>) -> Error {
    Error::Invalid(message.into())
}

fn exact(operation: &'static str, actual: usize, expected: usize) -> Result<()> {
    if actual != expected {
        return Err(Error::ShortTransfer {
            operation,
            actual,
            expected,
        });
    }
    Ok(())
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum CheckId {
    Usb,
    Access,
    Dfu,
    Rom,
    Model,
    Version,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum CheckStatus {
    Pending,
    Running,
    Passed,
    Failed,
    Skipped,
}

#[derive(Clone, Debug, Serialize)]
pub struct Check {
    pub id: CheckId,
    pub status: CheckStatus,
    pub detail: String,
}

#[derive(Clone, Debug, Serialize)]
#[serde(tag = "event", rename_all = "snake_case")]
pub enum Event {
    Check(Check),
    Progress {
        stage: CheckId,
        completed: usize,
        total: usize,
    },
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(tag = "state", content = "detail", rename_all = "snake_case")]
pub enum Cleanup {
    NotAttempted,
    Idle,
    Failed(String),
}

#[derive(Clone, Debug, Serialize)]
pub struct CheckReport {
    pub schema_version: u32,
    pub source: &'static str,
    pub device: Option<DeviceInfo>,
    pub checks: Vec<Check>,
    pub bootrom_sha256: Option<String>,
    pub identity: Option<Identity>,
    pub cleanup: Cleanup,
    /// BootROM and SysCfg match the supported target.
    pub compatible: bool,
}

impl CheckReport {
    fn new(source: &'static str, device: Option<DeviceInfo>) -> Self {
        Self {
            schema_version: 1,
            source,
            device,
            checks: [
                CheckId::Usb,
                CheckId::Access,
                CheckId::Dfu,
                CheckId::Rom,
                CheckId::Model,
                CheckId::Version,
            ]
            .into_iter()
            .map(|id| Check {
                id,
                status: CheckStatus::Pending,
                detail: String::new(),
            })
            .collect(),
            bootrom_sha256: None,
            identity: None,
            cleanup: Cleanup::NotAttempted,
            compatible: false,
        }
    }

    fn set(
        &mut self,
        id: CheckId,
        status: CheckStatus,
        detail: impl Into<String>,
        emit: &mut impl FnMut(Event),
    ) {
        let check = self.checks.iter_mut().find(|check| check.id == id).unwrap();
        check.status = status;
        check.detail = detail.into();
        emit(Event::Check(check.clone()));
    }

    fn skip_pending(&mut self, emit: &mut impl FnMut(Event)) {
        for check in &mut self.checks {
            if check.status == CheckStatus::Pending {
                check.status = CheckStatus::Skipped;
                check.detail = "A preceding check failed".into();
                emit(Event::Check(check.clone()));
            }
        }
    }

    fn record_rom(&mut self, bytes: &[u8], emit: &mut impl FnMut(Event)) -> Result<()> {
        exact("BootROM image", bytes.len(), BOOTROM_SIZE)?;
        let hash = format!("{:x}", Sha256::digest(bytes));
        self.bootrom_sha256 = Some(hash.clone());
        if hash != SUPPORTED_BOOTROM_SHA256 {
            return Err(invalid(format!(
                "Unsupported BootROM SHA-256 {hash}; NOR calls refused"
            )));
        }
        self.set(CheckId::Rom, CheckStatus::Passed, hash, emit);
        Ok(())
    }

    fn record_identity(&mut self, config: &SysCfg, emit: &mut impl FnMut(Event)) -> Result<()> {
        let identity = config.identity()?;
        let model_matches = matches!(identity.model.as_str(), "MC293" | "MC297")
            && identity.hardware_version == 0x00130200;
        let version_matches = identity.recorded_firmware == "2.0.4";
        self.set(
            CheckId::Model,
            if model_matches {
                CheckStatus::Passed
            } else {
                CheckStatus::Failed
            },
            format!(
                "{}; HwVr {:#010x}{}",
                identity.model,
                identity.hardware_version,
                if model_matches {
                    " (Classic Rev B target)"
                } else {
                    " (unsupported target)"
                }
            ),
            emit,
        );
        self.set(
            CheckId::Version,
            if version_matches {
                CheckStatus::Passed
            } else {
                CheckStatus::Failed
            },
            format!("SysCfg records {}", identity.recorded_firmware),
            emit,
        );
        self.identity = Some(identity);
        self.compatible = model_matches && version_matches;
        Ok(())
    }
}

/// Check a saved 64 KiB BootROM and SysCfg (or NOR starting with SysCfg).
pub fn check_saved(bootrom: &[u8], syscfg: &[u8]) -> CheckReport {
    let mut report = CheckReport::new("saved_files", None);
    let mut emit = |_| {};
    for id in [CheckId::Usb, CheckId::Access, CheckId::Dfu] {
        report.set(id, CheckStatus::Skipped, "Offline input", &mut emit);
    }
    if let Err(error) = report.record_rom(bootrom, &mut emit) {
        report.set(
            CheckId::Rom,
            CheckStatus::Failed,
            error.to_string(),
            &mut emit,
        );
    } else if let Err(error) =
        SysCfg::parse(syscfg).and_then(|cfg| report.record_identity(&cfg, &mut emit))
    {
        report.set(
            CheckId::Model,
            CheckStatus::Failed,
            error.to_string(),
            &mut emit,
        );
    }
    report.skip_pending(&mut emit);
    report
}
