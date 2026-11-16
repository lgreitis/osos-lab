// SPDX-License-Identifier: GPL-3.0-only

use crate::{
    dfu::{Dfu, Transport},
    invalid, payload, Cleanup, Error, Result,
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
        self.fast_exchange(&wire, size).map_err(|e| Error::Context {
            context: format!("Fast NOR offset {offset:#x}"),
            source: Box::new(e),
        })
    }

    pub(crate) fn dump_nor_checked(
        &mut self,
        emit: impl FnMut(NorProgress) -> Result<()>,
    ) -> Result<NorDump> {
        self.read_nor_checked(0, NOR_SIZE, emit)
    }

    pub(crate) fn read_nor_checked(
        &mut self,
        start_offset: usize,
        length: usize,
        mut emit: impl FnMut(NorProgress) -> Result<()>,
    ) -> Result<NorDump> {
        if length < 64
            || start_offset
                .checked_add(length)
                .is_none_or(|end| end > NOR_SIZE)
        {
            return Err(invalid("Invalid NOR read range"));
        }
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
            let mut bytes = Vec::with_capacity(length);
            let progress_step = length.div_ceil(100).clamp(512, 16384);
            let mut next_progress = progress_step;
            for offset in (0..length).step_by(512) {
                bytes.extend_from_slice(
                    &self.nor_fast_block(start_offset + offset, 512.min(length - offset))?,
                );
                if bytes.len() >= next_progress || bytes.len() == length {
                    emit(NorProgress {
                        stage: "nor_read",
                        completed: bytes.len(),
                        total: length,
                    })?;
                    next_progress = bytes.len() + progress_step;
                }
            }

            // Shifted boundaries expose SPI/FIFO tail errors.
            next_progress = progress_step;
            for offset in (1..length).step_by(509) {
                let size = 509.min(length - offset);
                let shifted = self.nor_fast_block(start_offset + offset, size)?;
                if shifted != bytes[offset..offset + size] {
                    return Err(invalid(format!(
                        "NOR shifted read disagrees at {offset:#x}"
                    )));
                }
                if offset + size >= next_progress || offset + size == length {
                    emit(NorProgress {
                        stage: "nor_verify",
                        completed: offset + size,
                        total: length,
                    })?;
                    next_progress = offset + size + progress_step;
                }
            }

            // Cross-check both edges through the 64-byte reply path.
            for offset in [0, length - 64] {
                if self.nor_bytes((start_offset + offset) as u32, 64, 60)?
                    != bytes[offset..offset + 64]
                {
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
            (result, Err(cleanup)) => Err(Error::Cleanup {
                step: Box::new(
                    result
                        .err()
                        .unwrap_or_else(|| invalid("NOR dump completed")),
                ),
                cleanup: Box::new(cleanup),
                advice: "; re-enter fresh DFU",
            }),
        }
    }
}
