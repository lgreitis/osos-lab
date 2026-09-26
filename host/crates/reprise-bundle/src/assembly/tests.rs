// SPDX-License-Identifier: GPL-3.0-only

use super::recipe::{update_ffs_checksum, MAX_RECIPE};
use super::*;
use crate::{Asset, Component, TrustedKey};
use serde_json::json;
use std::fs;

fn recipe_json() -> serde_json::Value {
    json!({
        "schema":2, "interface":INTERFACE,
        "inputs":{"osos":{"bytes":4,"sha256":sha256(b"base")}},
        "output":{"bytes":7},
        "data":{"bytes":3,"sha256":sha256(b"new")},
        "checks":[{"name":"osos","offset":1,"hex":"6173"}],
        "segments":[{"kind":"input","name":"osos","offset":0,"bytes":4},
                    {"kind":"data","offset":0,"bytes":3}]
    })
}

#[test]
fn recipes_validate_inputs_preimages_ranges_and_output() {
    let inputs = BTreeMap::from([("osos".into(), b"base".to_vec())]);
    let parse = |value: &serde_json::Value| Recipe::parse(&serde_json::to_vec(value).unwrap());
    let original = recipe_json();
    assert_eq!(
        parse(&original).unwrap().apply(&inputs, b"new").unwrap(),
        b"basenew"
    );
    for (path, value) in [
        ("/schema", json!(1)),
        ("/schema", json!(3)),
        ("/interface", json!("future")),
        ("/output/bytes", json!(MAX_OUTPUT + 1)),
        ("/output/bytes", json!(8)),
        ("/output/bytes", json!(6)),
        ("/data", json!(null)),
        ("/data/bytes", json!(4)),
        ("/data/sha256", json!("invalid")),
        ("/data/sha256", json!(sha256(b"wrong"))),
        ("/checks/0/name", json!("bds")),
        ("/checks/0/offset", json!(usize::MAX)),
        ("/checks/0/hex", json!("0000")),
        ("/checks/0/hex", json!("abc")),
        ("/checks/0/hex", json!("é")),
        ("/checks/0/hex", json!("")),
        ("/segments/0/name", json!("bds")),
        ("/segments/0/offset", json!(usize::MAX)),
        ("/segments/0/bytes", json!(usize::MAX)),
        ("/segments/0/bytes", json!(0)),
        ("/segments/1/bytes", json!(2)),
        ("/segments/1/bytes", json!(0)),
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
    assert!(parse(&original).unwrap().apply(&inputs, b"bad").is_err());
    let mut unchecked = original.clone();
    unchecked.as_object_mut().unwrap().remove("checks");
    assert_eq!(
        parse(&unchecked).unwrap().apply(&inputs, b"new").unwrap(),
        b"basenew"
    );
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
    let recipe = json!({"schema":2, "interface":INTERFACE,
        "inputs":{"osos":{"bytes":4,"sha256":sha256(b"base")}},
        "output":{"bytes":output.len()},
        "data":{"bytes":1,"sha256":sha256(&[0])},
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
fn local_and_bundle_companions_share_sysinfo_and_layout_checks() {
    let (bundle, inputs) = companion_bundle();
    let config = cfg();
    let mut nor = vec![0; 0x100000];
    for (offset, value) in [
        (0, 0x53436667u32),
        (4, (24 + config.entries.len() * 20) as u32),
        (8, 0x2000),
        (12, 0x10001),
        (20, config.entries.len() as u32),
    ] {
        nor[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    for (i, (tag, bytes)) in config.entries.iter().enumerate() {
        let offset = 24 + i * 20;
        for (j, byte) in tag.bytes().rev().enumerate() {
            nor[offset + j] = byte;
        }
        nor[offset + 4..offset + 20].copy_from_slice(bytes);
    }
    let directory = tempfile::tempdir().unwrap();
    fs::write(directory.path().join("osos.bin"), b"base").unwrap();
    let local = |nor: &[u8]| {
        assemble_local_companion(
            bundle.file("companion", "recipe").unwrap(),
            bundle.file("companion", "data").unwrap(),
            directory.path(),
            nor,
        )
    };
    assert_eq!(
        local(&nor).unwrap(),
        assemble_companion(&bundle, &inputs, &config).unwrap()
    );
    assert!(local(&nor[..nor.len() - 1]).is_err());
    nor[0] ^= 1;
    assert!(local(&nor).is_err());
    assert!(personalize_companion(vec![0; 1], &config).is_err());
    let mut occupied = vec![0; 0x2b800];
    occupied[SYSINFO_OFFSET] = 1;
    assert!(personalize_companion(occupied, &config).is_err());
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
#[ignore = "requires preserved local Apple images, recipes and build outputs"]
fn preserved_build_matches_python_export_and_rust_assembly() {
    let root = std::env::var_os("REPRISE_ASSEMBLY_ROOT")
        .map(std::path::PathBuf::from)
        .expect("REPRISE_ASSEMBLY_ROOT");
    let temporary = tempfile::tempdir().unwrap();
    let export = temporary.path().join("bundle");
    let result = std::process::Command::new("python3")
        .arg(root.join("tools/export_bundle.py"))
        .args(["--helper"])
        .arg(root.join("usb-helper/tests/fixtures"))
        .args(["--version", "0.1.0-test", "--out"])
        .arg(&export)
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

#[test]
fn local_and_bundle_paths_share_source_recipe_assembly() {
    let recipe = serde_json::to_vec(&recipe_json()).unwrap();
    let directory = tempfile::tempdir().unwrap();
    fs::write(directory.path().join("osos.bin"), b"base").unwrap();
    let local = assemble_local(&recipe, b"new", directory.path()).unwrap();
    let (mut bundle, inputs) = companion_bundle();
    component(
        &mut bundle,
        "osos",
        &[("recipe", recipe), ("data", b"new".to_vec())],
    );
    bundle.manifest.components.get_mut("osos").unwrap().format = "reprise-osos-recipe-v2".into();
    assert_eq!(local, assemble_component(&bundle, "osos", &inputs).unwrap());
    fs::write(directory.path().join("osos.bin"), b"oops").unwrap();
    assert!(assemble_local(
        &serde_json::to_vec(&recipe_json()).unwrap(),
        b"new",
        directory.path()
    )
    .is_err());
}

fn pe_image() -> Vec<u8> {
    let mut image = vec![0; 0x400];
    image[..2].copy_from_slice(b"MZ");
    image[0x80..0x84].copy_from_slice(b"PE\0\0");
    image[0x86..0x88].copy_from_slice(&1u16.to_le_bytes());
    image[0x94..0x96].copy_from_slice(&0xe0u16.to_le_bytes());
    image[0x98..0x9a].copy_from_slice(&0x10bu16.to_le_bytes());
    for (offset, value) in [
        (0x3c, 0x80u32),
        (0xd0, 0x400),
        (0x120, 0x300),
        (0x124, 12),
        (0x180, 0x200),
        (0x184, 0x200),
        (0x188, 0x200),
        (0x18c, 0x200),
        (0x220, 0x250),
        (0x300, 0),
        (0x304, 12),
    ] {
        image[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    image[0x308..0x30a].copy_from_slice(&0x3220u16.to_le_bytes());
    image
}

#[test]
fn pe_relocation_preserves_every_byte_except_highlow_sites() {
    let original = pe_image();
    let mut expected = original.clone();
    expected[0x220..0x224].copy_from_slice(&0x2201c550u32.to_le_bytes());
    assert_eq!(pe::relocate(&original, 0x2201c300, 1).unwrap(), expected);
    assert!(pe::relocate(&original, 0x2201c300, 2).is_err());
    assert!(pe::relocate(&original, u32::MAX, 1).is_err());
    for (offset, value) in [
        (0x3c, 0u32),
        (0xb4, 1),
        (0xd0, 0x500),
        (0x184, 0x201),
        (0x188, 0x300),
        (0x120, u32::MAX),
        (0x124, u32::MAX),
        (0x304, 7),
        (0x304, 14),
        (0x300, u32::MAX),
        (0x220, 0x400),
        (0x308, 0x2220),
        (0x308, 0x32203220),
        (0x308, 0x32213220),
    ] {
        let mut bad = original.clone();
        bad[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        assert!(
            pe::relocate(&bad, 0x2201c300, 1).is_err(),
            "{offset:#x} {value:#x}"
        );
    }
    for length in [0, 0x80, 0x124, 0x309] {
        assert!(pe::relocate(&original[..length], 0x2201c300, 1).is_err());
    }
}

#[test]
fn recipe_checks_original_bytes_before_relocation_and_copies_relocated_slices() {
    let image = pe_image();
    let inputs = BTreeMap::from([("bds".into(), image.clone())]);
    let mut spec = json!({
        "schema":2, "interface":INTERFACE,
        "inputs":{"bds":{"bytes":image.len(),"sha256":sha256(&image)}},
        "output":{"bytes":4}, "data":{"bytes":0,"sha256":sha256(&[])},
        "checks":[{"name":"bds","offset":0x220,"hex":"50020000"}],
        "relocations":{"bds":{"base":0x1000,"count":1}},
        "segments":[{"kind":"input","name":"bds","offset":0x220,"bytes":4}]
    });
    let apply = |value: &serde_json::Value| {
        Recipe::parse(&serde_json::to_vec(value).unwrap())?.apply(&inputs, &[])
    };
    assert_eq!(apply(&spec).unwrap(), 0x1250u32.to_le_bytes());
    spec["relocations"]["osos"] = json!({"base":0,"count":1});
    assert!(apply(&spec).is_err());
}

#[test]
fn ffs_checksum_updates_only_the_data_checksum_and_rejects_invalid_headers() {
    let mut image = vec![0; 32];
    image[18] = 4;
    image[19] = 0x40;
    image[20] = 32;
    image[16] = 0u8.wrapping_sub(4 + 0x40 + 32);
    image[23] = 0xf8;
    image[24..].copy_from_slice(b"new data");
    let original = image.clone();
    update_ffs_checksum(&mut image, 0).unwrap();
    assert_eq!(
        image[24..]
            .iter()
            .fold(image[17], |sum, byte| sum.wrapping_add(*byte)),
        0
    );
    image[17] = original[17];
    assert_eq!(image, original);
    for (offset, value) in [(16, 0), (18, 3), (19, 0), (20, 23), (20, 33)] {
        let mut bad = original.clone();
        bad[offset] = value;
        assert!(update_ffs_checksum(&mut bad, 0).is_err());
    }
    assert!(update_ffs_checksum(&mut image, usize::MAX).is_err());
    assert!(update_ffs_checksum(&mut image[..23], 0).is_err());
}
