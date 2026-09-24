// SPDX-License-Identifier: GPL-3.0-only
//! Prepare local Apple firmware inputs for assembly.

mod efi;

#[cfg(test)]
mod tests;

use crate::{
    assembly::{AppleInputs, INPUT_FILES},
    invalid, read_file, sha256, write_directory, Result,
};
use reprise_device::AppleNorImage;
use serde::{Deserialize, Serialize};
use std::{
    collections::BTreeMap,
    io::{Cursor, Read},
    path::Path,
};

const MAX_IPSW: usize = 128 * 1024 * 1024;
const OSOS_BYTES: usize = 10599920;
const OSOS_CIPHER_SHA256: &str = "206ddcfd838ca2876e557cabb14501296b28e445ba4495a660fa3c745ce36fd9";
const OSOS_SHA256: &str = "5841de6479f3a655102745fdb20e153ef99c4ef413314ccdb9a4721cace9a36e";

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "PascalCase")]
pub struct FirmwareMetadata {
    pub firmware_name: String,
    #[serde(rename = "BuildID")]
    pub build_id: u32,
    #[serde(rename = "VisibleBuildID")]
    pub visible_build_id: u32,
    #[serde(rename = "FamilyID")]
    pub family_id: u32,
    #[serde(rename = "UpdaterFamilyID")]
    pub updater_family_id: u32,
}

impl FirmwareMetadata {
    pub fn version(&self) -> String {
        format!(
            "{}.{}.{}",
            self.visible_build_id >> 24,
            (self.visible_build_id >> 20) & 15,
            (self.visible_build_id >> 16) & 15
        )
    }

    pub fn require_supported(&self) -> Result<()> {
        if self.family_id != 11
            || self.updater_family_id != 35
            || self.visible_build_id != 0x02048000
            || self.build_id != 0x09048000
        {
            return Err(invalid(format!(
                "Unsupported IPSW: version {}, family {}, updater family {}",
                self.version(),
                self.family_id,
                self.updater_family_id
            )));
        }
        Ok(())
    }
}

pub struct Ipsw {
    pub metadata: FirmwareMetadata,
    pub sha256: String,
    osos: Vec<u8>,
}

fn member(zip: &mut zip::ZipArchive<Cursor<&[u8]>>, name: &str, max: usize) -> Result<Vec<u8>> {
    let mut file = zip
        .by_name(name)
        .map_err(|e| invalid(format!("IPSW {name}: {e}")))?;
    if file.size() > max as u64 || file.is_dir() || file.is_symlink() {
        return Err(invalid(format!("Invalid or oversized IPSW member: {name}")));
    }
    let mut bytes = Vec::new();
    (&mut file).take(max as u64 + 1).read_to_end(&mut bytes)?;
    if bytes.len() > max || bytes.len() as u64 != file.size() {
        return Err(invalid("IPSW member size mismatch"));
    }
    Ok(bytes)
}

impl Ipsw {
    pub fn load(path: &Path) -> Result<Self> {
        Self::parse(&read_file(path, MAX_IPSW)?)
    }

    pub fn parse(bytes: &[u8]) -> Result<Self> {
        if bytes.len() > MAX_IPSW {
            return Err(invalid("IPSW exceeds 128 MiB"));
        }
        let mut zip = zip::ZipArchive::new(Cursor::new(bytes))
            .map_err(|e| invalid(format!("Invalid IPSW ZIP: {e}")))?;
        let manifest = member(&mut zip, "manifest.plist", 64 * 1024)?;
        #[derive(Deserialize)]
        struct Manifest {
            #[serde(rename = "FirmwarePayload")]
            payload: FirmwareMetadata,
        }
        let manifest: Manifest = plist::from_bytes(&manifest)
            .map_err(|e| invalid(format!("Invalid IPSW manifest: {e}")))?;
        manifest.payload.require_supported()?;
        let firmware = member(&mut zip, &manifest.payload.firmware_name, MAX_IPSW)?;
        let osos = extract_osos(&firmware)?;
        if osos.len() != OSOS_BYTES || sha256(&osos) != OSOS_CIPHER_SHA256 {
            return Err(invalid(
                "IPSW OSOS bytes do not match Classic Rev B / 2.0.4",
            ));
        }
        Ok(Self {
            metadata: manifest.payload,
            sha256: sha256(bytes),
            osos,
        })
    }

    pub fn validate_plaintext(&self, image: &[u8]) -> Result<()> {
        validate_osos(image)
    }

    pub fn encrypted_osos(&self) -> &[u8] {
        &self.osos
    }

    pub fn ciphertext(&self) -> &[u8] {
        &self.osos[0x800..]
    }

    /// Wrap the complete decrypted body, retaining its final AES block.
    pub fn wrap_plaintext(&self, body: &[u8]) -> Result<Vec<u8>> {
        if body.len() != self.ciphertext().len() {
            return Err(invalid("OSOS plaintext body length mismatch"));
        }
        let mut image = vec![0; 0x800];
        image[..8].copy_from_slice(b"87021.0\x02");
        image[8..12].copy_from_slice(&self.osos[8..12]);
        for offset in [12, 16, 20] {
            image[offset..offset + 4].copy_from_slice(&(body.len() as u32).to_le_bytes());
        }
        image.extend_from_slice(body);
        validate_osos(&image)?;
        Ok(image)
    }
}

fn word(b: &[u8], p: usize) -> usize {
    u32::from_le_bytes(b[p..p + 4].try_into().unwrap()) as usize
}

fn extract_osos(firmware: &[u8]) -> Result<Vec<u8>> {
    if firmware.len() < 0x5280
        || &firmware[0x100..0x104] != b"]ih["
        || word(firmware, 0x104) != 0x4000
        || &firmware[0x108..0x10c] != b"\x0c\x01\x03\x00"
    {
        return Err(invalid("Unsupported Classic firmware directory"));
    }
    let mut found = None;
    for header in firmware[0x5000..0x5280].chunks_exact(40) {
        if &header[..8] != b"!ATAsoso" {
            continue;
        }
        if found.is_some() {
            return Err(invalid("Duplicate OSOS directory entry"));
        }
        let start = word(header, 12);
        let length = word(header, 16)
            .checked_add(0x1000)
            .ok_or_else(|| invalid("OSOS length overflow"))?;
        let end = start
            .checked_add(length)
            .ok_or_else(|| invalid("OSOS range overflow"))?;
        if start < 0x6000 {
            return Err(invalid("OSOS overlaps firmware directory"));
        }
        let image = firmware
            .get(start..end)
            .ok_or_else(|| invalid("OSOS range outside firmware"))?;
        let body =
            reprise_device::classic_img1_body_range(image).map_err(|e| invalid(e.to_string()))?;
        found = Some(image[..body.end].to_vec());
    }
    found.ok_or_else(|| invalid("IPSW has no Classic OSOS image"))
}

fn validate_osos(image: &[u8]) -> Result<()> {
    if image.len() != OSOS_BYTES || sha256(image) != OSOS_SHA256 {
        return Err(invalid(
            "Decrypted OSOS does not match the supported 2.0.4 input",
        ));
    }
    Ok(())
}

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
        ipsw.metadata.require_supported()?;
        validate_osos(osos)?;
        let image = AppleNorImage::locate(nor).map_err(|e| invalid(e.to_string()))?;
        let loader = if image.encrypted() {
            saved_loader
                .ok_or_else(|| invalid("Encrypted NOR requires its decrypted Apple loader"))?
        } else {
            image.plaintext().map_err(|e| invalid(e.to_string()))?
        };
        AppleNorImage::validate_plaintext(loader).map_err(|e| invalid(e.to_string()))?;
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
