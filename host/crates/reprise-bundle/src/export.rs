// SPDX-License-Identifier: GPL-3.0-only
//! Build and package local exports using the same validation as bundle consumers.

use crate::{
    asset_filename, invalid, load_assets, read_file, sha256, write_directory, Asset, Compatibility,
    Component, Manifest, Purpose, Result, VerifiedBundle, MANIFEST_FILE, MAX_ASSET_BYTES,
    MAX_BUNDLE_BYTES, MAX_MANIFEST_BYTES, SIGNATURE_FILE,
};
use serde::Deserialize;
use std::{collections::BTreeMap, fs, io::Write, path::Path};
use zip::{write::SimpleFileOptions, CompressionMethod, ZipWriter};

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Specification {
    purpose: Purpose,
    version: String,
    minimum_installer_version: String,
    compatibility: Compatibility,
    components: BTreeMap<String, Component>,
}

pub fn export(specification: &Path, base: &Path, output: &Path) -> Result<Manifest> {
    let spec: Specification =
        serde_json::from_slice(&read_file(specification, MAX_MANIFEST_BYTES)?)?;
    let mut manifest = Manifest {
        schema: 1,
        purpose: spec.purpose,
        version: spec.version,
        minimum_installer_version: spec.minimum_installer_version,
        compatibility: spec.compatibility,
        components: spec.components,
        assets: BTreeMap::new(),
    };
    let mut assets = BTreeMap::new();
    let mut total = 0;
    for component in manifest.components.values_mut() {
        for filename in component.files.values_mut() {
            let bytes = read_file(&base.join(&*filename), MAX_ASSET_BYTES)?;
            let hash = sha256(&bytes);
            if !assets.contains_key(&hash) {
                total += bytes.len();
                if total > MAX_BUNDLE_BYTES {
                    return Err(invalid("Bundle too large"));
                }
                manifest
                    .assets
                    .insert(hash.clone(), Asset { bytes: bytes.len() });
                assets.insert(hash.clone(), bytes);
            }
            *filename = hash;
        }
    }
    manifest.validate(&manifest.minimum_installer_version)?;
    let mut raw = serde_json::to_vec_pretty(&manifest)?;
    raw.push(b'\n');
    if raw.len() > MAX_MANIFEST_BYTES {
        return Err(invalid("Manifest too large"));
    }
    let bundle = VerifiedBundle {
        manifest,
        assets,
        digest: sha256(&raw),
    }
    .check_helper()?;
    let mut files = BTreeMap::from([(MANIFEST_FILE.to_owned(), raw)]);
    for (hash, bytes) in bundle.assets {
        files.insert(asset_filename(&hash), bytes);
    }
    if let Some(parent) = output.parent().filter(|p| !p.as_os_str().is_empty()) {
        fs::create_dir_all(parent)?;
    }
    write_directory(
        output,
        files
            .iter()
            .map(|(name, data)| (name.as_str(), data.as_slice())),
    )?;
    Ok(bundle.manifest)
}

pub fn pack(directory: &Path, output: &Path) -> Result<Manifest> {
    let raw = read_file(&directory.join(MANIFEST_FILE), MAX_MANIFEST_BYTES)?;
    let manifest: Manifest = serde_json::from_slice(&raw)?;
    manifest.validate(&manifest.minimum_installer_version)?;
    let assets = load_assets(directory, &manifest)?;
    let bundle = VerifiedBundle {
        manifest,
        assets,
        digest: sha256(&raw),
    }
    .check_helper()?;
    let mut files = BTreeMap::from([(MANIFEST_FILE.to_owned(), raw)]);
    for (hash, bytes) in bundle.assets {
        files.insert(asset_filename(&hash), bytes);
    }
    let signature = directory.join(SIGNATURE_FILE);
    if signature.try_exists()? || signature.is_symlink() {
        let bytes = read_file(&signature, 64)?;
        if bytes.len() != 64 {
            return Err(invalid("Invalid signature length"));
        }
        files.insert(SIGNATURE_FILE.to_owned(), bytes);
    }
    let parent = output
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    fs::create_dir_all(parent)?;
    let mut temporary = tempfile::NamedTempFile::new_in(parent)?;
    {
        let mut archive = ZipWriter::new(temporary.as_file_mut());
        let options = SimpleFileOptions::default()
            .compression_method(CompressionMethod::Deflated)
            .unix_permissions(0o644);
        for (name, bytes) in files {
            archive
                .start_file(name, options)
                .map_err(|e| invalid(e.to_string()))?;
            archive.write_all(&bytes)?;
        }
        archive.finish().map_err(|e| invalid(e.to_string()))?;
    }
    temporary.as_file().sync_all()?;
    temporary.persist_noclobber(output).map_err(|e| e.error)?;
    Ok(bundle.manifest)
}
