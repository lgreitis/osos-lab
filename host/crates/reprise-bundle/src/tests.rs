// SPDX-License-Identifier: GPL-3.0-only

use super::*;

pub(crate) struct Fixture {
    pub dir: tempfile::TempDir,
    pub key: TrustedKey,
}

impl Fixture {
    pub fn new() -> Self {
        let dir = tempfile::tempdir().unwrap();
        let image = b"synthetic image; not executable firmware";
        let descriptor = b"synthetic descriptor";
        let image_hash = sha256(image);
        let descriptor_hash = sha256(descriptor);
        let manifest = serde_json::json!({
            "schema": 1, "purpose": "development", "version": "0.1.0",
            "minimum_installer_version": "0.1.0",
            "compatibility": {
                "target": "classic7g-2.0.4", "models": ["MC293", "MC297"],
                "hardware_version": 0x00130200, "apple_firmware": "2.0.4",
                "bootrom_sha256": "69c087afc5753d7f0f11f09b141b372af753a854bc526673ea444c486d6003e4"
            },
            "components": {"usb_helper": {"format": "reprise-upload-v3", "files": {"image": image_hash, "descriptor": descriptor_hash}}},
            "assets": {image_hash.clone(): {"bytes": image.len()}, descriptor_hash.clone(): {"bytes": descriptor.len()}}
        });
        fs::write(
            dir.path().join(MANIFEST_FILE),
            serde_json::to_vec_pretty(&manifest).unwrap(),
        )
        .unwrap();
        fs::write(dir.path().join(asset_filename(&image_hash)), image).unwrap();
        fs::write(
            dir.path().join(asset_filename(&descriptor_hash)),
            descriptor,
        )
        .unwrap();
        let public = sign_directory(dir.path(), &"17".repeat(32), "0.1.0").unwrap();
        Self {
            dir,
            key: TrustedKey::from_hex(&public).unwrap(),
        }
    }

    fn load(&self) -> Result<VerifiedBundle> {
        VerifiedBundle::load(self.dir.path(), &self.key, "0.1.0")
    }
}

#[test]
fn signature_and_device_compatibility() {
    let f = Fixture::new();
    let b = f.load().unwrap();
    let c = &b.manifest().compatibility;
    assert!(b
        .require_device("MC293", c.hardware_version, "2.0.4", &c.bootrom_sha256)
        .is_ok());
    assert!(b
        .require_device("MC999", c.hardware_version, "2.0.4", &c.bootrom_sha256)
        .is_err());
    assert!(b
        .require_device("MC293", 0, "2.0.4", &c.bootrom_sha256)
        .is_err());
    assert!(b
        .require_device("MC293", c.hardware_version, "2.0.1", &c.bootrom_sha256)
        .is_err());
    assert!(b.file("nor", "image").is_err());
    let wrong = SigningKey::from_bytes(&[4; 32]);
    let key = TrustedKey::from_hex(&hex(&wrong.verifying_key().to_bytes())).unwrap();
    assert!(VerifiedBundle::load(f.dir.path(), &key, "0.1.0").is_err());
    assert!(VerifiedBundle::load(f.dir.path(), &f.key, "0.0.9").is_err());
    assert!(sign_directory(f.dir.path(), &"17".repeat(32), "0.1.0").is_err());
}

#[test]
fn tampered_manifest_signature_and_assets_are_rejected() {
    for file in [MANIFEST_FILE, SIGNATURE_FILE, "asset"] {
        let f = Fixture::new();
        let name = if file == "asset" {
            asset_filename(f.load().unwrap().manifest().assets.keys().next().unwrap())
        } else {
            file.to_owned()
        };
        let path = f.dir.path().join(name);
        let mut bytes = fs::read(&path).unwrap();
        bytes[0] ^= 1;
        fs::write(&path, bytes).unwrap();
        assert!(f.load().is_err(), "{file}");
    }
}

#[test]
fn manifest_contract_rejects_incomplete_releases_paths_unknown_formats_and_limits() {
    let f = Fixture::new();
    let original = f.load().unwrap().manifest().clone();
    let mut bad = original.clone();
    bad.purpose = Purpose::Release;
    assert!(bad.validate("0.1.0").is_err());
    let mut bad = original.clone();
    bad.components.get_mut("usb_helper").unwrap().format = "future".into();
    assert!(bad.validate("0.1.0").is_err());
    let mut bad = original.clone();
    bad.components
        .get_mut("usb_helper")
        .unwrap()
        .files
        .insert("../escape".into(), "../escape".into());
    assert!(bad.validate("0.1.0").is_err());
    let mut bad = original.clone();
    bad.assets.values_mut().next().unwrap().bytes = MAX_ASSET_BYTES + 1;
    assert!(bad.validate("0.1.0").is_err());
    let mut bad = original.clone();
    bad.assets.insert("0".repeat(64), Asset { bytes: 1 });
    assert!(bad.validate("0.1.0").is_err());
    let mut bad = original.clone();
    bad.compatibility.models = vec!["MC293".into(), "MC293".into()];
    assert!(bad.validate("0.1.0").is_err());
    let mut bad = original;
    bad.minimum_installer_version = "0.2.0".into();
    assert!(bad.validate("0.1.0").is_err());
}

#[test]
fn python_export_is_loadable_and_deterministic() {
    let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../usb-helper/tests/fixtures");
    let tool = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../tools/bundle.py");
    let tmp = tempfile::tempdir().unwrap();
    let mut digests = Vec::new();
    for name in ["one", "two"] {
        let out = tmp.path().join(name);
        let result = std::process::Command::new("python3")
            .arg(&tool)
            .arg("--helper")
            .arg(&fixture)
            .args(["--version", "0.1.0-dev.1+local.01", "--out"])
            .arg(&out)
            .output()
            .unwrap();
        assert!(
            result.status.success(),
            "{}",
            String::from_utf8_lossy(&result.stderr)
        );
        let public = sign_directory(&out, &"18".repeat(32), "0.1.0").unwrap();
        let bundle =
            VerifiedBundle::load(&out, &TrustedKey::from_hex(&public).unwrap(), "0.1.0").unwrap();
        assert_eq!(
            bundle.file("usb_helper", "image").unwrap(),
            fs::read(fixture.join("upload.dfu")).unwrap()
        );
        let archive = tmp.path().join(format!("{name}.zip"));
        let result = std::process::Command::new("python3")
            .arg(&tool)
            .arg("--pack")
            .arg(&out)
            .arg("--out")
            .arg(&archive)
            .output()
            .unwrap();
        assert!(
            result.status.success(),
            "{}",
            String::from_utf8_lossy(&result.stderr)
        );
        let packed =
            VerifiedBundle::load_zip(&archive, &TrustedKey::from_hex(&public).unwrap(), "0.1.0")
                .unwrap();
        assert_eq!(packed.digest(), bundle.digest());
        assert_eq!(
            packed.file("usb_helper", "image").unwrap(),
            bundle.file("usb_helper", "image").unwrap()
        );
        digests.push(bundle.digest().to_owned());
    }
    assert_eq!(digests[0], digests[1]);
}

#[cfg(unix)]
#[test]
fn assets_cannot_be_symlinks() {
    let f = Fixture::new();
    let bundle = f.load().unwrap();
    let hash = bundle.manifest().assets.keys().next().unwrap();
    let path = f.dir.path().join(asset_filename(hash));
    let moved = f.dir.path().join("original");
    fs::rename(&path, &moved).unwrap();
    std::os::unix::fs::symlink(moved, path).unwrap();
    assert!(f.load().is_err());
}
