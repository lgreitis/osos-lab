// SPDX-License-Identifier: GPL-3.0-only
//! Firmware release assets and offline assembly.

mod archive;
pub mod assembly;
mod distribution;
pub mod preparation;

#[cfg(test)]
mod tests;

pub use distribution::fetch;
use ed25519_dalek::{Signature, Signer, SigningKey, VerifyingKey};
use semver::Version;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    path::Path,
};

pub const MAX_MANIFEST_BYTES: usize = 256 * 1024;
pub const MAX_ASSET_BYTES: usize = 32 * 1024 * 1024;
pub const MAX_BUNDLE_BYTES: usize = 64 * 1024 * 1024;
pub const MANIFEST_FILE: &str = "manifest.json";
pub const SIGNATURE_FILE: &str = "manifest.json.sig";

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("{0}")]
    Invalid(String),
    #[error(transparent)]
    Io(#[from] std::io::Error),
    #[error(transparent)]
    Json(#[from] serde_json::Error),
    #[error(transparent)]
    Network(#[from] reqwest::Error),
}

pub type Result<T> = std::result::Result<T, Error>;

fn invalid(message: impl Into<String>) -> Error {
    Error::Invalid(message.into())
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Purpose {
    Development,
    Release,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Compatibility {
    pub target: String,
    pub models: Vec<String>,
    pub hardware_version: u32,
    pub apple_firmware: String,
    pub bootrom_sha256: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Asset {
    pub bytes: usize,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Component {
    pub format: String,
    /// Logical file name to SHA-256; the corresponding asset is `<sha256>.blob`.
    pub files: BTreeMap<String, String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Manifest {
    pub schema: u32,
    pub purpose: Purpose,
    pub version: String,
    pub minimum_installer_version: String,
    pub compatibility: Compatibility,
    pub components: BTreeMap<String, Component>,
    pub assets: BTreeMap<String, Asset>,
}

impl Manifest {
    pub fn validate(&self, installer_version: &str) -> Result<()> {
        Version::parse(&self.version).map_err(|_| invalid("Invalid bundle version"))?;
        let minimum = Version::parse(&self.minimum_installer_version)
            .map_err(|_| invalid("Invalid minimum installer version"))?;
        let installer =
            Version::parse(installer_version).map_err(|_| invalid("Invalid installer version"))?;
        if self.schema != 1 || installer < minimum {
            return Err(invalid("Unsupported bundle schema or installer version"));
        }
        let c = &self.compatibility;
        if c.target != "classic7g-2.0.4"
            || c.hardware_version != 0x00130200
            || c.apple_firmware != "2.0.4"
            || c.bootrom_sha256
                != "69c087afc5753d7f0f11f09b141b372af753a854bc526673ea444c486d6003e4"
            || c.models.is_empty()
            || c.models.iter().any(|m| m != "MC293" && m != "MC297")
            || c.models.iter().collect::<BTreeSet<_>>().len() != c.models.len()
        {
            return Err(invalid("Unsupported bundle target"));
        }
        if self.components.is_empty() || self.assets.is_empty() {
            return Err(invalid("Invalid bundle component/asset count"));
        }
        let mut referenced = BTreeSet::new();
        for (name, component) in &self.components {
            let (format, required): (&str, &[&str]) = match name.as_str() {
                "usb_helper" => ("reprise-upload-v3", &["image", "descriptor"]),
                "osos" => ("reprise-osos-recipe-v2", &["recipe", "data"]),
                "companion" => ("reprise-companion-recipe-v2", &["recipe", "data"]),
                "nor" => ("reprise-nor-template-v1", &["image", "descriptor"]),
                _ => return Err(invalid("Unknown bundle component")),
            };
            if component.format != format
                || required
                    .iter()
                    .any(|key| !component.files.contains_key(*key))
            {
                return Err(invalid(format!("Invalid {name} component")));
            }
            for (name, hash) in &component.files {
                if !valid_name(name) || !self.assets.contains_key(hash) {
                    return Err(invalid("Invalid component asset reference"));
                }
                referenced.insert(hash);
            }
        }
        if self.purpose == Purpose::Release
            && ["usb_helper", "osos", "companion", "nor"]
                .iter()
                .any(|name| !self.components.contains_key(*name))
        {
            return Err(invalid("Release bundle requires all four components"));
        }
        let mut total = 0usize;
        for (hash, asset) in &self.assets {
            if !valid_hash(hash)
                || !referenced.contains(hash)
                || asset.bytes == 0
                || asset.bytes > MAX_ASSET_BYTES
            {
                return Err(invalid("Invalid or unreferenced bundle asset"));
            }
            total = total
                .checked_add(asset.bytes)
                .ok_or_else(|| invalid("Bundle too large"))?;
        }
        if total > MAX_BUNDLE_BYTES {
            return Err(invalid("Bundle too large"));
        }
        Ok(())
    }
}

/// A key supplied by the application or explicitly selected by the CLI user.
pub struct TrustedKey(VerifyingKey);

impl TrustedKey {
    pub fn from_hex(value: &str) -> Result<Self> {
        let bytes = decode_hex::<32>(value.trim())?;
        let key =
            VerifyingKey::from_bytes(&bytes).map_err(|_| invalid("Invalid signing public key"))?;
        if key.is_weak() {
            return Err(invalid("Weak signing public key"));
        }
        Ok(Self(key))
    }
}

pub struct VerifiedBundle {
    manifest: Manifest,
    digest: String,
    assets: BTreeMap<String, Vec<u8>>,
}

impl VerifiedBundle {
    pub fn load(directory: &Path, key: &TrustedKey, installer_version: &str) -> Result<Self> {
        let raw = read_file(&directory.join(MANIFEST_FILE), MAX_MANIFEST_BYTES)?;
        let signature = read_file(&directory.join(SIGNATURE_FILE), 64)?;
        let manifest = authenticate(&raw, &signature, key, installer_version)?;
        let assets = load_assets(directory, &manifest)?;
        Ok(Self {
            manifest,
            digest: sha256(&raw),
            assets,
        })
    }

    pub fn manifest(&self) -> &Manifest {
        &self.manifest
    }

    pub fn digest(&self) -> &str {
        &self.digest
    }

    pub fn file(&self, component: &str, name: &str) -> Result<&[u8]> {
        let hash = self
            .manifest
            .components
            .get(component)
            .and_then(|c| c.files.get(name))
            .ok_or_else(|| invalid(format!("Missing bundle file: {component}/{name}")))?;
        Ok(&self.assets[hash])
    }

    pub fn require_device(
        &self,
        model: &str,
        hardware_version: u32,
        firmware: &str,
        bootrom_sha256: &str,
    ) -> Result<()> {
        let c = &self.manifest.compatibility;
        if !c.models.iter().any(|m| m == model)
            || c.hardware_version != hardware_version
            || c.apple_firmware != firmware
            || c.bootrom_sha256 != bootrom_sha256
        {
            return Err(invalid("Bundle does not support the checked device"));
        }
        Ok(())
    }
}

/// Sign a staged export, returning the public key as lowercase hex.
pub fn sign_directory(
    directory: &Path,
    secret_hex: &str,
    installer_version: &str,
) -> Result<String> {
    let raw = read_file(&directory.join(MANIFEST_FILE), MAX_MANIFEST_BYTES)?;
    let manifest: Manifest = serde_json::from_slice(&raw)?;
    manifest.validate(installer_version)?;
    load_assets(directory, &manifest)?;
    let key = SigningKey::from_bytes(&decode_hex::<32>(secret_hex.trim())?);
    write_new(&directory.join(SIGNATURE_FILE), &key.sign(&raw).to_bytes())?;
    Ok(hex(&key.verifying_key().to_bytes()))
}

fn authenticate(
    raw: &[u8],
    signature: &[u8],
    key: &TrustedKey,
    installer_version: &str,
) -> Result<Manifest> {
    if raw.len() > MAX_MANIFEST_BYTES {
        return Err(invalid("Manifest too large"));
    }
    let signature = Signature::from_slice(signature)
        .map_err(|_| invalid("Invalid manifest signature length"))?;
    key.0
        .verify_strict(raw, &signature)
        .map_err(|_| invalid("Manifest signature verification failed"))?;
    let manifest: Manifest = serde_json::from_slice(raw)?;
    manifest.validate(installer_version)?;
    Ok(manifest)
}

fn load_assets(directory: &Path, manifest: &Manifest) -> Result<BTreeMap<String, Vec<u8>>> {
    let mut result = BTreeMap::new();
    for (hash, asset) in &manifest.assets {
        let bytes = read_file(&directory.join(asset_filename(hash)), asset.bytes)?;
        check_asset(hash, asset, &bytes)?;
        result.insert(hash.clone(), bytes);
    }
    Ok(result)
}

fn check_asset(hash: &str, asset: &Asset, bytes: &[u8]) -> Result<()> {
    if bytes.len() != asset.bytes || sha256(bytes) != hash {
        return Err(invalid(format!("Bundle asset size/hash mismatch: {hash}")));
    }
    Ok(())
}

fn asset_filename(hash: &str) -> String {
    format!("{hash}.blob")
}

fn sha256(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

fn valid_hash(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c))
}

fn valid_name(value: &str) -> bool {
    !value.is_empty()
        && value
            .bytes()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == b'_')
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn decode_hex<const N: usize>(value: &str) -> Result<[u8; N]> {
    if value.len() != N * 2 || !value.bytes().all(|c| c.is_ascii_hexdigit()) {
        return Err(invalid(format!(
            "Expected {} hexadecimal characters",
            N * 2
        )));
    }
    let mut bytes = [0; N];
    for (i, byte) in bytes.iter_mut().enumerate() {
        *byte =
            u8::from_str_radix(&value[i * 2..i * 2 + 2], 16).map_err(|_| invalid("Invalid hex"))?;
    }
    Ok(bytes)
}

fn read_file(path: &Path, limit: usize) -> Result<Vec<u8>> {
    if !fs::symlink_metadata(path)?.file_type().is_file() {
        return Err(invalid("Bundle inputs must be regular files"));
    }
    let mut bytes = Vec::new();
    File::open(path)?
        .take(limit as u64 + 1)
        .read_to_end(&mut bytes)?;
    if bytes.len() > limit {
        return Err(invalid("Bundle input exceeds size limit"));
    }
    Ok(bytes)
}

fn write_new(path: &Path, bytes: &[u8]) -> Result<()> {
    let mut file = OpenOptions::new().write(true).create_new(true).open(path)?;
    file.write_all(bytes)?;
    file.sync_all()?;
    Ok(())
}

fn write_directory<'a>(
    directory: &Path,
    files: impl IntoIterator<Item = (&'a str, &'a [u8])>,
) -> Result<()> {
    let parent = directory
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    let staging = tempfile::tempdir_in(parent)?;
    for (name, bytes) in files {
        let path = staging.path().join(name);
        fs::create_dir_all(path.parent().unwrap())?;
        write_new(&path, bytes)?;
    }
    fs::create_dir(directory)?;
    let result = (|| -> Result<()> {
        for entry in fs::read_dir(staging.path())? {
            let entry = entry?;
            fs::rename(entry.path(), directory.join(entry.file_name()))?;
        }
        Ok(())
    })();
    if result.is_err() {
        let _ = fs::remove_dir_all(directory);
    }
    result
}
