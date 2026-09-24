// SPDX-License-Identifier: GPL-3.0-only

use crate::{invalid, Result, Session};
use std::time::Duration;

impl Session {
    /// Send the dual-boot NOR installer. The device installs and restarts.
    pub fn install_bootloader(
        mut self,
        image: &[u8],
        emit: impl FnMut(usize, usize),
    ) -> Result<()> {
        if !self.operation_ready {
            return Err(invalid("Bootloader installation requires checked DFU"));
        }
        if image.len() < 0x800 || image.len() > 0x20000 || &image[..8] != b"87021.0\x03" {
            return Err(invalid("Invalid Classic NOR installer image"));
        }
        self.operation_ready = false;
        self.dfu.transport.timeout = Duration::from_secs(2);
        self.dfu.idle()?;
        self.dfu.download_image(image, emit)
    }
}
