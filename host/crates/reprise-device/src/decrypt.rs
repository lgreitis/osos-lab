// SPDX-License-Identifier: GPL-3.0-only

use crate::{
    dfu::{Dfu, Transport},
    invalid, payload, Cleanup, Error, Result,
};
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::time::Instant;

#[derive(Clone, Copy, Debug)]
pub struct DecryptOptions {
    /// First-block IV; interior slices use the preceding ciphertext block.
    pub iv: [u8; 16],
    pub chunk_size: usize,
}

impl Default for DecryptOptions {
    fn default() -> Self {
        Self {
            iv: [0; 16],
            chunk_size: 512,
        }
    }
}

impl DecryptOptions {
    pub fn validate(&self, ciphertext: &[u8], reference: Option<&[u8]>) -> Result<()> {
        if ciphertext.is_empty() || !ciphertext.len().is_multiple_of(16) {
            return Err(invalid("Ciphertext must be nonempty and 16-byte aligned"));
        }
        if !(16..=512).contains(&self.chunk_size) || !self.chunk_size.is_multiple_of(16) {
            return Err(invalid("AES chunk must be 16..512 bytes, aligned to 16"));
        }
        if reference.is_some_and(|r| r.len() != ciphertext.len()) {
            return Err(invalid(
                "Plaintext reference must match the ciphertext length",
            ));
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct DecryptProgress {
    pub stage: &'static str,
    pub completed: usize,
    pub total: usize,
}

#[derive(Debug, Serialize)]
pub struct DecryptReport {
    pub schema_version: u32,
    pub bytes: usize,
    pub calls: usize,
    /// Elapsed time for the AES loop.
    pub seconds: f64,
    pub kib_per_second: f64,
    pub sha256: String,
    /// True when a supplied plaintext reference matched.
    pub reference_matched: bool,
    pub cleanup: Cleanup,
}

#[derive(Debug)]
pub struct Decrypted {
    pub plaintext: Vec<u8>,
    pub report: DecryptReport,
}

impl<T: Transport> Dfu<T> {
    pub(crate) fn aes_exchange(
        &mut self,
        ciphertext: &[u8],
        iv: &[u8; 16],
        code: &[u8],
    ) -> Result<Vec<u8>> {
        let size = ciphertext.len();
        if !(16..=512).contains(&size)
            || !size.is_multiple_of(16)
            || code.is_empty()
            || payload::PREFIX_SIZE + code.len() > 0x400
        {
            return Err(invalid("Invalid fast AES exchange layout"));
        }
        let mut wire = vec![0; payload::PREFIX_SIZE];
        wire[..4].copy_from_slice(&u32::MAX.to_le_bytes());
        wire[16..16 + size].copy_from_slice(ciphertext);
        wire[16 + size..32 + size].copy_from_slice(iv);
        wire.extend_from_slice(code);
        self.fast_exchange(&wire, size)
    }

    pub(crate) fn decrypt_key_checked(
        &mut self,
        ciphertext: &[u8],
        options: DecryptOptions,
        reference: Option<&[u8]>,
        key: u32,
        emit: impl FnMut(DecryptProgress) -> Result<()>,
    ) -> Result<Decrypted> {
        let mut plaintext = Vec::with_capacity(ciphertext.len());
        let report =
            self.decrypt_key_into(ciphertext, options, reference, key, &mut plaintext, emit)?;
        Ok(Decrypted { plaintext, report })
    }

    /// Appends each decrypted chunk to `plaintext` as it arrives, so the
    /// chunks before a failure stay with the caller.
    pub(crate) fn decrypt_key_into(
        &mut self,
        ciphertext: &[u8],
        options: DecryptOptions,
        reference: Option<&[u8]>,
        key: u32,
        plaintext: &mut Vec<u8>,
        mut emit: impl FnMut(DecryptProgress) -> Result<()>,
    ) -> Result<DecryptReport> {
        options.validate(ciphertext, reference)?;
        let base = plaintext.len();
        let operation = (|| {
            if self.state()? != 2 {
                return Err(invalid(
                    "AES requires dfuIDLE after successful device checks",
                ));
            }
            self.fast_echo(|size| {
                emit(DecryptProgress {
                    stage: "echo",
                    completed: size,
                    total: 528,
                })
            })?;
            let full_code = payload::fast_aes_key(options.chunk_size, true, key)?;
            let remainder = ciphertext.len() % options.chunk_size;
            let tail_code = if remainder != 0 {
                payload::fast_aes_key(remainder, true, key)?
            } else {
                Vec::new()
            };
            let start = Instant::now();
            plaintext.reserve(ciphertext.len());
            let mut iv = options.iv;
            let mut calls = 0;
            let mut next_percent = 1;
            for block in ciphertext.chunks(options.chunk_size) {
                let offset = plaintext.len() - base;
                let code = if block.len() == options.chunk_size {
                    &full_code
                } else {
                    &tail_code
                };
                let output = self
                    .aes_exchange(block, &iv, code)
                    .map_err(|e| Error::Context {
                        context: format!("AES at input offset {offset:#x}"),
                        source: Box::new(e),
                    })?;
                if reference.is_some_and(|r| output != r[offset..offset + block.len()]) {
                    return Err(invalid(format!(
                        "Plaintext reference mismatch at input offset {offset:#x}"
                    )));
                }
                iv.copy_from_slice(&block[block.len() - 16..]);
                plaintext.extend_from_slice(&output);
                calls += 1;
                let done = plaintext.len() - base;
                if done == ciphertext.len() || done * 100 >= next_percent * ciphertext.len() {
                    emit(DecryptProgress {
                        stage: "decrypt",
                        completed: done,
                        total: ciphertext.len(),
                    })?;
                    next_percent = done * 100 / ciphertext.len() + 1;
                }
            }
            let seconds = start.elapsed().as_secs_f64();
            let bytes = plaintext.len() - base;
            Ok(DecryptReport {
                schema_version: 1,
                bytes,
                calls,
                seconds,
                kib_per_second: bytes as f64 / 1024.0 / seconds.max(f64::EPSILON),
                sha256: format!("{:x}", Sha256::digest(&plaintext[base..])),
                reference_matched: reference.is_some(),
                cleanup: Cleanup::NotAttempted,
            })
        })();
        match (operation, self.idle()) {
            (Ok(mut report), Ok(())) => {
                report.cleanup = Cleanup::Idle;
                Ok(report)
            }
            (Err(error), Ok(())) => Err(error),
            (result, Err(cleanup)) => Err(Error::Cleanup {
                step: Box::new(result.err().unwrap_or_else(|| invalid("AES completed"))),
                cleanup: Box::new(cleanup),
                advice: "; re-enter fresh DFU",
            }),
        }
    }
}
