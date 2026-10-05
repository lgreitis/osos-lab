// SPDX-License-Identifier: GPL-3.0-only
//! Prepare local Apple firmware inputs for assembly.

mod efi;
pub use efi::extract_all as extract_efi_modules;

#[cfg(test)]
mod tests;

mod ipsw;
#[cfg(test)]
use ipsw::extract_osos;
pub use ipsw::{unpack_ipsw, FirmwareMetadata, Ipsw};

use crate::{
    assembly::{AppleInputs, INPUT_FILES},
    invalid, write_directory, Result,
};
use reprise_device::AppleNorImage;
use std::{collections::BTreeMap, path::Path};

pub struct PreparedInputs {
    files: BTreeMap<String, Vec<u8>>,
}

impl PreparedInputs {
    /// Extract assembly inputs from verified plaintext images and a NOR backup.
    pub fn from_plaintext(
        ipsw: &Ipsw,
        nor: &[u8],
        osos: &[u8],
        saved_loader: Option<&[u8]>,
    ) -> Result<Self> {
        let target = ipsw.metadata.target()?;
        let identity = reprise_device::SysCfg::parse(nor)
            .and_then(|cfg| cfg.identity())
            .map_err(|e| invalid(e.to_string()))?;
        if !target.compatibility.matches_identity(
            &identity.model,
            identity.hardware_version,
            &identity.recorded_firmware,
        ) {
            return Err(invalid("IPSW and NOR belong to different targets"));
        }
        ipsw.validate_plaintext(osos)?;
        let image = AppleNorImage::locate(nor).map_err(|e| invalid(e.to_string()))?;
        let loader = if image.encrypted() {
            saved_loader
                .ok_or_else(|| invalid("Encrypted NOR requires its decrypted Apple loader"))?
        } else {
            image.plaintext().map_err(|e| invalid(e.to_string()))?
        };
        image
            .validate_plaintext(loader)
            .map_err(|e| invalid(e.to_string()))?;
        if saved_loader.is_some_and(|saved| saved != loader) {
            return Err(invalid("Saved Apple loader disagrees with NOR"));
        }
        let mut files = efi::extract(loader)?;
        files.insert("osos.bin".into(), osos.to_vec());
        files.insert("apple-loader.bin".into(), loader.to_vec());
        Ok(Self { files })
    }

    pub fn apple_inputs(&self) -> Result<AppleInputs> {
        let mut inputs = AppleInputs::default();
        for (key, filename) in INPUT_FILES {
            inputs.insert(key, self.files[*filename].clone())?;
        }
        Ok(inputs)
    }

    /// Write assembly inputs to a new directory.
    pub fn write(&self, directory: &Path) -> Result<()> {
        write_directory(
            directory,
            self.files
                .iter()
                .map(|(name, bytes)| (name.as_str(), bytes.as_slice())),
        )
    }
}
