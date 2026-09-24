// SPDX-License-Identifier: GPL-3.0-only

use crate::{
    dfu::{Dfu, Transport},
    invalid, payload, Cleanup, Result,
};
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::time::Instant;

pub const NOR_SIZE: usize = 0x100000;

#[derive(Clone, Debug, Serialize)]
pub struct NorProgress {
    pub stage: &'static str,
    pub completed: usize,
    pub total: usize,
}

#[derive(Debug, Serialize)]
pub struct NorReport {
    pub schema_version: u32,
    pub bytes: usize,
    pub sha256: String,
    /// Elapsed time for both read passes and edge checks.
    pub seconds: f64,
    pub cross_checked: bool,
    pub cleanup: Cleanup,
}

#[derive(Debug)]
pub struct NorDump {
    pub bytes: Vec<u8>,
    pub report: NorReport,
}

impl<T: Transport> Dfu<T> {
    fn nor_fast_block(&mut self, offset: usize, size: usize) -> Result<Vec<u8>> {
        let code = payload::fast_nor(offset as u32, size)?;
        let mut wire = vec![0; payload::PREFIX_SIZE];
        wire.extend_from_slice(&code);
        self.fast_exchange(&wire, size)
            .map_err(|e| invalid(format!("Fast NOR offset {offset:#x}: {e}")))
    }

    pub(crate) fn dump_nor_checked(
        &mut self,
        mut emit: impl FnMut(NorProgress) -> Result<()>,
    ) -> Result<NorDump> {
        let operation = (|| {
            if self.state()? != 2 {
                return Err(invalid(
                    "NOR dump requires dfuIDLE after successful device checks",
                ));
            }
            self.fast_echo(|n| {
                emit(NorProgress {
                    stage: "echo",
                    completed: n,
                    total: 528,
                })
            })?;
            self.execute(&payload::nor_init())?;
            let start = Instant::now();
            let mut bytes = Vec::with_capacity(NOR_SIZE);
            let mut next_progress = 16384;
            for offset in (0..NOR_SIZE).step_by(512) {
                bytes.extend_from_slice(&self.nor_fast_block(offset, 512.min(NOR_SIZE - offset))?);
                if bytes.len() >= next_progress || bytes.len() == NOR_SIZE {
                    emit(NorProgress {
                        stage: "nor_read",
                        completed: bytes.len(),
                        total: NOR_SIZE,
                    })?;
                    next_progress = bytes.len() + 16384;
                }
            }

            // Shifted boundaries expose SPI/FIFO tail errors.
            next_progress = 16384;
            for offset in (1..NOR_SIZE).step_by(509) {
                let size = 509.min(NOR_SIZE - offset);
                let shifted = self.nor_fast_block(offset, size)?;
                if shifted != bytes[offset..offset + size] {
                    return Err(invalid(format!(
                        "NOR shifted read disagrees at {offset:#x}"
                    )));
                }
                if offset + size >= next_progress || offset + size == NOR_SIZE {
                    emit(NorProgress {
                        stage: "nor_verify",
                        completed: offset + size,
                        total: NOR_SIZE,
                    })?;
                    next_progress = offset + size + 16384;
                }
            }

            // Cross-check both edges through the 64-byte reply path.
            for offset in [0, NOR_SIZE - 64] {
                if self.nor_bytes(offset as u32, 64, 60)? != bytes[offset..offset + 64] {
                    return Err(invalid(format!("NOR edge read disagrees at {offset:#x}")));
                }
            }
            let report = NorReport {
                schema_version: 1,
                bytes: bytes.len(),
                sha256: format!("{:x}", Sha256::digest(&bytes)),
                seconds: start.elapsed().as_secs_f64(),
                cross_checked: true,
                cleanup: Cleanup::NotAttempted,
            };
            Ok(NorDump { bytes, report })
        })();
        match (operation, self.idle()) {
            (Ok(mut dump), Ok(())) => {
                dump.report.cleanup = Cleanup::Idle;
                Ok(dump)
            }
            (Err(error), Ok(())) => Err(error),
            (result, Err(cleanup)) => Err(invalid(format!(
                "{}; DFU cleanup failed: {cleanup}; re-enter fresh DFU",
                result
                    .err()
                    .map_or_else(|| "NOR dump completed".into(), |e| e.to_string())
            ))),
        }
    }
}
