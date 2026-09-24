// SPDX-License-Identifier: GPL-3.0-only

use crate::{
    asset_filename, authenticate, check_asset, invalid, sha256, Manifest, Result, TrustedKey,
    VerifiedBundle, MANIFEST_FILE, MAX_MANIFEST_BYTES, SIGNATURE_FILE,
};
use std::{collections::BTreeMap, fs::File, io::Read, path::Path};
use zip::ZipArchive;

impl VerifiedBundle {
    pub fn load_zip(path: &Path, key: &TrustedKey, installer_version: &str) -> Result<Self> {
        Self::read_zip(path, installer_version, Some(key))
    }

    /// Validate a user-selected package's compatibility and asset hashes.
    pub fn load_local_zip(path: &Path, installer_version: &str) -> Result<Self> {
        Self::read_zip(path, installer_version, None)
    }

    fn read_zip(path: &Path, installer_version: &str, key: Option<&TrustedKey>) -> Result<Self> {
        let mut archive = ZipArchive::new(File::open(path)?).map_err(|e| invalid(e.to_string()))?;
        let raw = read_member(&mut archive, MANIFEST_FILE, MAX_MANIFEST_BYTES)?;
        let manifest = if let Some(key) = key {
            let signature = read_member(&mut archive, SIGNATURE_FILE, 64)?;
            authenticate(&raw, &signature, key, installer_version)?
        } else {
            let manifest: Manifest = serde_json::from_slice(&raw)?;
            manifest.validate(installer_version)?;
            manifest
        };
        let mut assets = BTreeMap::new();
        for (hash, asset) in &manifest.assets {
            let bytes = read_member(&mut archive, &asset_filename(hash), asset.bytes)?;
            check_asset(hash, asset, &bytes)?;
            assets.insert(hash.clone(), bytes);
        }
        Ok(Self {
            manifest,
            digest: sha256(&raw),
            assets,
        })
    }
}

fn read_member(archive: &mut ZipArchive<File>, name: &str, limit: usize) -> Result<Vec<u8>> {
    let file = archive
        .by_name(name)
        .map_err(|e| invalid(format!("{name}: {e}")))?;
    if !file.is_file() || file.is_symlink() || file.size() > limit as u64 {
        return Err(invalid(format!("Invalid bundle member: {name}")));
    }
    let mut bytes = Vec::new();
    file.take(limit as u64 + 1).read_to_end(&mut bytes)?;
    if bytes.len() > limit {
        return Err(invalid(format!("Bundle member exceeds size limit: {name}")));
    }
    Ok(bytes)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{tests::Fixture, SIGNATURE_FILE};
    use std::{fs, io::Write};
    use zip::{write::SimpleFileOptions, ZipWriter};

    fn pack(fixture: &Fixture, path: &Path) {
        let mut archive = ZipWriter::new(File::create(path).unwrap());
        for entry in fs::read_dir(fixture.dir.path()).unwrap() {
            let entry = entry.unwrap();
            archive
                .start_file(
                    entry.file_name().to_str().unwrap(),
                    SimpleFileOptions::default(),
                )
                .unwrap();
            archive.write_all(&fs::read(entry.path()).unwrap()).unwrap();
        }
        archive.finish().unwrap();
    }

    #[test]
    fn local_packages_accept_unsigned_content_and_enforce_compatibility() {
        let fixture = Fixture::new();
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("bundle.zip");
        fs::remove_file(fixture.dir.path().join(SIGNATURE_FILE)).unwrap();
        pack(&fixture, &path);
        let bundle = VerifiedBundle::load_local_zip(&path, "0.1.0").unwrap();
        assert_eq!(bundle.manifest().version, "0.1.0");
        assert!(VerifiedBundle::load_local_zip(&path, "0.0.9").is_err());
        assert!(VerifiedBundle::load_zip(&path, &fixture.key, "0.1.0").is_err());

        let manifest_path = fixture.dir.path().join(MANIFEST_FILE);
        let mut manifest = bundle.manifest().clone();
        manifest.compatibility.models = vec!["MC999".into()];
        fs::write(manifest_path, serde_json::to_vec(&manifest).unwrap()).unwrap();
        pack(&fixture, &path);
        assert!(VerifiedBundle::load_local_zip(&path, "0.1.0").is_err());
    }

    #[test]
    fn missing_corrupt_and_oversized_assets_are_rejected() {
        for replacement in [None, Some(vec![0; 20]), Some(vec![0; 1024])] {
            let fixture = Fixture::new();
            let directory = tempfile::tempdir().unwrap();
            let path = directory.path().join("bundle.zip");
            let bundle = VerifiedBundle::load(fixture.dir.path(), &fixture.key, "0.1.0").unwrap();
            let hash = &bundle.manifest().components["usb_helper"].files["descriptor"];
            let asset = fixture.dir.path().join(asset_filename(hash));
            if let Some(bytes) = replacement {
                fs::write(asset, bytes).unwrap();
            } else {
                fs::remove_file(asset).unwrap();
            }
            pack(&fixture, &path);
            assert!(VerifiedBundle::load_local_zip(&path, "0.1.0").is_err());
            assert!(VerifiedBundle::load_zip(&path, &fixture.key, "0.1.0").is_err());
        }
    }

    #[test]
    fn unrelated_archive_paths_are_never_extracted() {
        let fixture = Fixture::new();
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("bundle.zip");
        pack(&fixture, &path);
        let file = fs::OpenOptions::new()
            .read(true)
            .write(true)
            .open(&path)
            .unwrap();
        let mut archive = ZipWriter::new_append(file).unwrap();
        archive
            .start_file("../outside", SimpleFileOptions::default())
            .unwrap();
        archive.write_all(b"ignored").unwrap();
        archive.finish().unwrap();
        VerifiedBundle::load_local_zip(&path, "0.1.0").unwrap();
        assert_eq!(fs::read_dir(directory.path()).unwrap().count(), 1);
    }
}
