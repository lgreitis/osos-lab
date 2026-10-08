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
use std::{collections::BTreeMap, path::Path};

pub struct PreparedInputs {
    files: BTreeMap<String, Vec<u8>>,
}

impl PreparedInputs {
    /// Extract assembly inputs from verified OSOS and AUPD plaintext.
    pub fn from_plaintext(ipsw: &Ipsw, osos: &[u8], aupd: &[u8]) -> Result<Self> {
        let target = ipsw.metadata.target()?;
        ipsw.validate_plaintext(osos)?;
        if !target.source.aupd_body.matches(aupd) {
            return Err(invalid("AUPD plaintext does not match the pinned firmware"));
        }
        let start = target.source.loader_offset;
        let loader = aupd
            .get(start..start + target.inputs["apple-loader.bin"].bytes)
            .ok_or_else(|| invalid("AUPD loader outside image"))?;
        if !target.inputs["apple-loader.bin"].matches(loader) {
            return Err(invalid("AUPD loader fingerprint mismatch"));
        }
        let mut files = efi::extract(loader)?;
        files.retain(|name, _| target.inputs.contains_key(name));
        for (name, data) in &files {
            if !target.inputs[name].matches(data) {
                return Err(invalid(format!("Unexpected AUPD module: {name}")));
            }
        }
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
