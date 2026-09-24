// SPDX-License-Identifier: GPL-3.0-only

use super::*;
use crate::{Asset, Component, TrustedKey};
use serde_json::json;
use std::fs;

fn recipe_json() -> serde_json::Value {
    json!({
        "schema":1, "interface":INTERFACE,
        "inputs":{"osos":{"bytes":4,"sha256":sha256(b"base")}},
        "output":{"bytes":7,"sha256":sha256(b"basenew")},
        "segments":[{"kind":"input","name":"osos","offset":0,"bytes":4},
                    {"kind":"data","offset":0,"bytes":3}]
    })
}

#[test]
fn recipes_reject_bad_inputs_ranges_versions_and_results() {
    let inputs = BTreeMap::from([("osos".into(), b"base".to_vec())]);
    let parse = |value: &serde_json::Value| Recipe::parse(&serde_json::to_vec(value).unwrap());
    let original = recipe_json();
    assert_eq!(
        parse(&original).unwrap().apply(&inputs, b"new").unwrap(),
        b"basenew"
    );
    for (path, value) in [
        ("/schema", json!(2)),
        ("/interface", json!("future")),
        ("/output/bytes", json!(MAX_OUTPUT + 1)),
        ("/output/sha256", json!(sha256(b"wrong"))),
        ("/segments/0/name", json!("bds")),
        ("/segments/0/offset", json!(usize::MAX)),
        ("/segments/0/bytes", json!(usize::MAX)),
        ("/segments/0/bytes", json!(0)),
        ("/segments/1/bytes", json!(2)),
        ("/segments/1/offset", json!(1)),
    ] {
        let mut bad = original.clone();
        *bad.pointer_mut(path).unwrap() = value;
        assert!(
            parse(&bad).and_then(|r| r.apply(&inputs, b"new")).is_err(),
            "{path}"
        );
    }
    assert!(parse(&original)
        .unwrap()
        .apply(&BTreeMap::new(), b"new")
        .is_err());
    let bad = BTreeMap::from([("osos".into(), b"oops".to_vec())]);
    assert!(parse(&original).unwrap().apply(&bad, b"new").is_err());
    let mut bad = original.clone();
    bad["segments"][0]["script"] = json!("anything");
    assert!(parse(&bad).is_err());
    assert!(Recipe::parse(&vec![b' '; MAX_RECIPE + 1]).is_err());
}

fn cfg() -> SysCfg {
    let mut cfg = SysCfg {
        size: 204,
        entries: BTreeMap::new(),
    };
    for (name, bytes) in [
        ("SrNm", b"FIRSTDEVICE".as_slice()),
        ("Mod#", b"MC293"),
        ("SwVr", b"2.0.4"),
        ("HwId", &[0; 16]),
        ("FwId", &[1; 16]),
        ("Codc", &[2; 16]),
        ("HwVr", &[0, 0, 0, 0, 0, 2, 0x13, 0]),
        ("Regn", &[1, 0, 0, 0, b'U', b'S', 0, 0]),
    ] {
        let mut data = [0; 16];
        data[..bytes.len()].copy_from_slice(bytes);
        cfg.entries.insert(name.into(), data);
    }
    cfg
}

fn component(bundle: &mut VerifiedBundle, name: &str, files: &[(&str, Vec<u8>)]) {
    let mut refs = BTreeMap::new();
    for (file, data) in files {
        let hash = sha256(data);
        bundle.assets.insert(hash.clone(), data.clone());
        bundle
            .manifest
            .assets
            .insert(hash.clone(), Asset { bytes: data.len() });
        refs.insert((*file).to_owned(), hash);
    }
    bundle.manifest.components.insert(
        name.to_owned(),
        Component {
            format: String::new(),
            files: refs,
        },
    );
}

fn companion_bundle() -> (VerifiedBundle, AppleInputs) {
    let fixture = crate::tests::Fixture::new();
    let mut bundle = VerifiedBundle::load(fixture.dir.path(), &fixture.key, "0.1.0").unwrap();
    let output = vec![0; 0x2b800];
    let recipe = json!({"schema":1, "interface":INTERFACE,
        "inputs":{"osos":{"bytes":4,"sha256":sha256(b"base")}},
        "output":{"bytes":output.len(),"sha256":sha256(&output)},
        "segments":[{"kind":"zero","bytes":output.len()}]});
    component(
        &mut bundle,
        "companion",
        &[
            ("recipe", serde_json::to_vec(&recipe).unwrap()),
            ("data", vec![0]),
        ],
    );
    let mut inputs = AppleInputs::default();
    inputs.insert("osos", b"base".to_vec()).unwrap();
    (bundle, inputs)
}

#[test]
fn companion_uses_the_supplied_device_and_rejects_incompatible_config() {
    let (bundle, inputs) = companion_bundle();
    let mut config = cfg();
    let first = assemble_companion(&bundle, &inputs, &config).unwrap();
    assert_eq!(
        &first[SYSINFO_OFFSET + 0x18..SYSINFO_OFFSET + 0x23],
        b"FIRSTDEVICE"
    );
    assert_eq!(&first[SYSINFO_OFFSET + 0x92..SYSINFO_OFFSET + 0x94], b"US");
    config.entries.get_mut("SrNm").unwrap()[0] = b'X';
    let second = assemble_companion(&bundle, &inputs, &config).unwrap();
    let changed: Vec<_> = first
        .iter()
        .zip(&second)
        .enumerate()
        .filter(|(_, (a, b))| a != b)
        .map(|(i, _)| i)
        .collect();
    assert_eq!(changed, [SYSINFO_OFFSET + 0x18]);
    config.entries.remove("Codc");
    assert!(assemble_companion(&bundle, &inputs, &config).is_err());
    let mut config = cfg();
    config.entries.get_mut("Mod#").unwrap()[4] = b'9';
    assert!(assemble_companion(&bundle, &inputs, &config).is_err());
}

#[test]
fn output_is_created_exclusively_with_a_report() {
    let directory = tempfile::tempdir().unwrap();
    let out = directory.path().join("assembled");
    let artifacts = Artifacts {
        osos: vec![1, 2],
        companion: vec![3],
        nor_installer: vec![4],
    };
    let report = artifacts.write(&out).unwrap();
    assert_eq!(fs::read(out.join("osos-cfw.bin")).unwrap(), [1, 2]);
    assert_eq!(report.files["cfw-loader.bin"].sha256, sha256(&[3]));
    assert!(artifacts.write(&out).is_err());
    assert_eq!(fs::read(out.join("osos-cfw.bin")).unwrap(), [1, 2]);
}

#[test]
fn nor_template_requires_dual_boot_packaging() {
    let (mut bundle, _) = companion_bundle();
    let offset = 0xb10usize;
    let mut original = vec![0; offset + 16];
    original[..8].copy_from_slice(b"87021.0\x03");
    original[0x310..0x318].copy_from_slice(b"87021.0\x02");
    original[0x31c..0x320].copy_from_slice(&16u32.to_le_bytes());
    for (position, value, accepted) in [
        (0x350, 0, true),
        (0x350, 1, false),
        (0x317, 3, false),
        (0x31c, 32, false),
        (0x31c, 0, false),
    ] {
        let mut image = original.clone();
        image[position] = value;
        let descriptor = json!({"schema":1, "interface":INTERFACE,
            "bytes":image.len(), "sha256":sha256(&image), "bootloader_offset":offset});
        component(
            &mut bundle,
            "nor",
            &[
                ("image", image),
                ("descriptor", serde_json::to_vec(&descriptor).unwrap()),
            ],
        );
        assert_eq!(nor_installer(&bundle).is_ok(), accepted);
    }
}

#[test]
#[ignore = "requires preserved local Apple images, build outputs and ARM nm"]
fn preserved_build_matches_python_export_and_rust_assembly() {
    let root = std::env::var_os("REPRISE_ASSEMBLY_ROOT")
        .map(std::path::PathBuf::from)
        .expect("REPRISE_ASSEMBLY_ROOT");
    let nm = std::env::var_os("REPRISE_ASSEMBLY_NM").expect("REPRISE_ASSEMBLY_NM");
    let temporary = tempfile::tempdir().unwrap();
    let export = temporary.path().join("bundle");
    let result = std::process::Command::new("python3")
        .arg(root.join("tools/assembly.py"))
        .args(["--helper"])
        .arg(root.join("usb-helper/tests/fixtures"))
        .args(["--version", "0.1.0-test", "--out"])
        .arg(&export)
        .arg("--nm")
        .arg(nm)
        .output()
        .unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    let public = crate::sign_directory(&export, &"17".repeat(32), "0.1.0").unwrap();
    let bundle =
        VerifiedBundle::load(&export, &TrustedKey::from_hex(&public).unwrap(), "0.1.0").unwrap();
    let nor = fs::read(root.join("inputs/nor.bin")).unwrap();
    let ipsw_path = std::env::var_os("REPRISE_TEST_IPSW").expect("REPRISE_TEST_IPSW");
    let ipsw = crate::preparation::Ipsw::load(Path::new(&ipsw_path)).unwrap();
    let osos = fs::read(root.join("inputs/osos.bin")).unwrap();
    let loader = fs::read(root.join("inputs/apple-loader.bin")).unwrap();
    let inputs =
        crate::preparation::PreparedInputs::from_plaintext(&ipsw, &nor, &osos, Some(&loader))
            .unwrap()
            .apple_inputs()
            .unwrap();
    let artifacts = assemble(&bundle, &inputs, &nor).unwrap();
    for (name, bytes) in artifacts.files() {
        assert_eq!(
            bytes,
            fs::read(root.join("build").join(name)).unwrap(),
            "{name}"
        );
    }
    let mut config = SysCfg::parse(&nor).unwrap();
    config.entries.get_mut("SrNm").unwrap()[0] ^= 1;
    let modified = assemble_companion(&bundle, &inputs, &config).unwrap();
    assert_ne!(modified, artifacts.companion);
    assert_eq!(nor_installer(&bundle).unwrap(), artifacts.nor_installer);
    assert!(assemble(&bundle, &inputs, &nor[..nor.len() - 1]).is_err());
    let report = artifacts
        .write(&temporary.path().join("assembled"))
        .unwrap();
    assert_eq!(report.files.len(), 3);
}

#[test]
#[ignore = "requires a local distribution ZIP and saved Apple inputs"]
fn local_distribution_replays_from_saved_inputs() {
    let path = |name| std::path::PathBuf::from(std::env::var_os(name).expect(name));
    let root = path("REPRISE_ASSEMBLY_ROOT");
    let build = path("REPRISE_ASSEMBLY_BUILD");
    let bundle = VerifiedBundle::load_local_zip(&path("REPRISE_TEST_PACKAGE"), "0.1.0").unwrap();
    let helper = reprise_device::UploadHelper::from_bytes(
        bundle.file("usb_helper", "image").unwrap(),
        bundle.file("usb_helper", "descriptor").unwrap(),
    )
    .unwrap();
    assert!(helper.supports_storage_inspection());
    let nor = fs::read(root.join("inputs/nor.bin")).unwrap();
    let ipsw = crate::preparation::Ipsw::load(&path("REPRISE_TEST_IPSW")).unwrap();
    let osos = fs::read(root.join("inputs/osos.bin")).unwrap();
    let loader = fs::read(root.join("inputs/apple-loader.bin")).unwrap();
    let prepared =
        crate::preparation::PreparedInputs::from_plaintext(&ipsw, &nor, &osos, Some(&loader))
            .unwrap();
    let artifacts = assemble(&bundle, &prepared.apple_inputs().unwrap(), &nor).unwrap();
    assert_eq!(
        disk_bytes(&bundle).unwrap() as usize,
        artifacts.osos.len() + artifacts.companion.len()
    );
    for (name, bytes) in artifacts.files() {
        assert_eq!(bytes, fs::read(build.join(name)).unwrap(), "{name}");
    }
}
