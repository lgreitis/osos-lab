// SPDX-License-Identifier: GPL-3.0-only

use crate::{invalid, read_file, sha256, write_directory, Result};
use reprise_device::targets::{self, Target};
use serde::{Deserialize, Serialize};
use std::{
    collections::BTreeMap,
    io::{Cursor, Read},
    path::Path,
};

const MAX_IPSW: usize = 128 * 1024 * 1024;
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

    pub fn target(&self) -> Result<&'static Target> {
        targets::all()
            .iter()
            .find(|target| {
                let ipsw = &target.ipsw;
                (
                    ipsw.family_id,
                    ipsw.updater_family_id,
                    ipsw.visible_build_id,
                    ipsw.build_id,
                ) == (
                    self.family_id,
                    self.updater_family_id,
                    self.visible_build_id,
                    self.build_id,
                )
            })
            .ok_or_else(|| {
                invalid(format!(
                    "Unsupported IPSW: version {}, family {}, updater family {}",
                    self.version(),
                    self.family_id,
                    self.updater_family_id,
                ))
            })
    }

    pub fn require_supported(&self) -> Result<()> {
        self.target().map(|_| ())
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
        if !manifest
            .payload
            .target()?
            .ipsw
            .encrypted_osos
            .matches(&osos)
        {
            return Err(invalid("IPSW OSOS bytes do not match its firmware build"));
        }
        Ok(Self {
            metadata: manifest.payload,
            sha256: sha256(bytes),
            osos,
        })
    }

    pub fn validate_plaintext(&self, image: &[u8]) -> Result<()> {
        if image.len() != self.osos.len()
            || image.get(..8) != Some(b"87021.0\x02")
            || image[8..12] != self.osos[8..12]
            || [12, 16, 20]
                .iter()
                .any(|offset| word(image, *offset) != self.ciphertext().len())
        {
            return Err(invalid("Decrypted OSOS header or length mismatch"));
        }
        if !self.metadata.target()?.inputs["osos.bin"].matches(image) {
            return Err(invalid(
                "Decrypted OSOS does not match its target fingerprint",
            ));
        }
        Ok(())
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
        self.validate_plaintext(&image)?;
        Ok(image)
    }
}

fn word(b: &[u8], p: usize) -> usize {
    u32::from_le_bytes(b[p..p + 4].try_into().unwrap()) as usize
}

fn firmware_entries(firmware: &[u8]) -> Result<BTreeMap<String, std::ops::Range<usize>>> {
    if firmware.len() < 0x5280
        || &firmware[0x100..0x104] != b"]ih["
        || word(firmware, 0x104) != 0x4000
        || &firmware[0x108..0x10c] != b"\x0c\x01\x03\x00"
    {
        return Err(invalid("Unsupported Classic firmware directory"));
    }
    let mut entries = BTreeMap::new();
    for header in firmware[0x5000..0x5280].chunks_exact(40) {
        if header[..8] == [0; 8] {
            continue;
        }
        if &header[..4] != b"!ATA" || !header[4..8].iter().all(u8::is_ascii_alphanumeric) {
            return Err(invalid("Invalid firmware directory entry"));
        }
        let name: String = header[4..8].iter().rev().map(|b| *b as char).collect();
        let start = word(header, 12);
        // Directory lengths exclude the trailing 4 KiB container overhead.
        let end = word(header, 16)
            .checked_add(0x1000)
            .and_then(|n| start.checked_add(n))
            .ok_or_else(|| invalid("Firmware entry range overflow"))?;
        if start < 0x6000 || end > firmware.len() {
            return Err(invalid(
                "Firmware entry outside payload or overlapping directory",
            ));
        }
        if entries
            .values()
            .any(|r: &std::ops::Range<usize>| start < r.end && r.start < end)
        {
            return Err(invalid("Overlapping firmware entries"));
        }
        if entries.insert(name, start..end).is_some() {
            return Err(invalid("Duplicate firmware directory entry"));
        }
    }
    Ok(entries)
}

pub(super) fn extract_osos(firmware: &[u8]) -> Result<Vec<u8>> {
    let entries = firmware_entries(firmware)?;
    let range = entries
        .get("osos")
        .ok_or_else(|| invalid("IPSW has no Classic OSOS image"))?;
    let image = &firmware[range.clone()];
    let body =
        reprise_device::classic_img1_body_range(image).map_err(|e| invalid(e.to_string()))?;
    Ok(image[..body.end].to_vec())
}

/// Preserve the ZIP members and directory entries, including image signatures.
pub fn unpack_ipsw(path: &Path, directory: &Path) -> Result<serde_json::Value> {
    let bytes = read_file(path, MAX_IPSW)?;
    let ipsw = Ipsw::parse(&bytes)?;
    let mut zip = zip::ZipArchive::new(Cursor::new(bytes.as_slice()))
        .map_err(|e| invalid(format!("Invalid IPSW ZIP: {e}")))?;
    let firmware = member(&mut zip, &ipsw.metadata.firmware_name, MAX_IPSW)?;
    let mut inventory = Vec::new();
    let mut files = BTreeMap::new();
    for (name, range) in firmware_entries(&firmware)? {
        let data = firmware[range.clone()].to_vec();
        let file = format!("{name}.bin");
        inventory.push(serde_json::json!({
            "name": name, "file": file, "offset": range.start,
            "bytes": data.len(), "sha256": sha256(&data),
        }));
        files.insert(file, data);
    }
    let report = serde_json::json!({
        "metadata": ipsw.metadata, "ipsw_sha256": ipsw.sha256,
        "firmware_sha256": sha256(&firmware), "entries": inventory,
        "osos_img1_bytes": ipsw.osos.len(), "osos_img1_sha256": sha256(&ipsw.osos),
    });
    files.insert(
        "manifest.plist".into(),
        member(&mut zip, "manifest.plist", 64 * 1024)?,
    );
    files.insert("firmware.bin".into(), firmware);
    files.insert("osos.encrypted.img1".into(), ipsw.osos);
    files.insert("inventory.json".into(), serde_json::to_vec_pretty(&report)?);
    write_directory(
        directory,
        files
            .iter()
            .map(|(name, data)| (name.as_str(), data.as_slice())),
    )?;
    Ok(report)
}
