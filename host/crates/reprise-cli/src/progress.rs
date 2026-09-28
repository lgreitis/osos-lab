// SPDX-License-Identifier: GPL-3.0-only

use std::{
    io::{self, Write},
    time::{Duration, Instant},
};

#[derive(Default)]
pub(super) struct Progress {
    last: Option<(&'static str, u64, Instant)>,
}

impl Progress {
    pub fn write(
        &mut self,
        stage: &str,
        completed: u64,
        total: u64,
        json: bool,
        out: &mut impl Write,
    ) -> io::Result<()> {
        if json {
            return Ok(());
        }
        let label = match stage {
            "hash" => "Hashing input",
            "launch" => "Starting upload",
            "upload" => "Uploading",
            "write" => "Writing file",
            "verify" => "Verifying file",
            "dfu-return" => "Returning to DFU",
            "decrypt" => "Decrypting",
            "nor_read" => "Reading NOR",
            "nor_verify" => "Verifying NOR",
            _ => return Ok(()),
        };
        let percent = completed
            .min(total)
            .saturating_mul(100)
            .checked_div(total)
            .unwrap_or(100);
        let now = Instant::now();
        if let Some((previous, value, printed)) = self.last {
            if previous == label
                && (percent == value
                    || (percent < 100
                        && (percent < value + 5
                            || now.duration_since(printed) < Duration::from_secs(1))))
            {
                return Ok(());
            }
        }
        if total <= 1 {
            writeln!(out, "{label}…")?;
        } else {
            writeln!(out, "{label}: {percent}%")?;
        }
        self.last = Some((label, percent, now));
        Ok(())
    }
}
