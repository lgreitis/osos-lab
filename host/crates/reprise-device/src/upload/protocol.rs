// SPDX-License-Identifier: GPL-3.0-only
use super::UploadOptions;
use crate::{exact, invalid, Error, Result};

pub(super) const RESULT_ADDR: u32 = 0x2201fa80;
pub(super) const RESULT_SIZE: usize = 320;
pub(super) const STATUS: u8 = 0x52;
pub(super) const START: u8 = 0x53;
pub(super) const ACKNOWLEDGE: u8 = 0x54;
pub(super) const CANCEL: u8 = 0x55;
pub(super) const READY: u32 = 1;
pub(super) const VERIFYING: u32 = 3;
pub(super) const COMPLETE: u32 = 4;

#[derive(Debug, Clone, PartialEq, Eq)]
pub(super) struct Status {
    pub(super) raw: Vec<u8>,
}

impl Status {
    pub(super) fn parse(raw: &[u8], nonce: &[u8; 16], size: u32) -> Result<Self> {
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

    pub(super) fn state(&self) -> u32 {
        word(&self.raw, 24)
    }

    pub(super) fn rc(&self) -> i32 {
        word(&self.raw, 28) as i32
    }

    pub(super) fn received(&self) -> u32 {
        word(&self.raw, 36)
    }
    pub(super) fn written(&self) -> u32 {
        word(&self.raw, 40)
    }
    pub(super) fn verified(&self) -> u32 {
        word(&self.raw, 44)
    }
    pub(super) fn endpoint(&self) -> u32 {
        word(&self.raw, 48)
    }
    pub(super) fn error_no(&self) -> u32 {
        word(&self.raw, 56)
    }

    pub(super) fn complete(&self, sha: &[u8; 32], options: &UploadOptions) -> Result<()> {
        let b = &self.raw;
        if self.rc() != 0 {
            return Err(helper_error(self.rc(), self.error_no()));
        }
        if self.state() != COMPLETE
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

pub(super) fn helper_error(rc: i32, errno: u32) -> Error {
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

pub(super) fn word(b: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(b[at..at + 4].try_into().unwrap())
}
