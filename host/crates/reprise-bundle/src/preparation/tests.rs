// SPDX-License-Identifier: GPL-3.0-only

use super::*;
use std::{fs, io::Write};

fn metadata() -> FirmwareMetadata {
    FirmwareMetadata {
        firmware_name: "Firmware-35.9.0.4".into(),
        build_id: 0x09048000,
        visible_build_id: 0x02048000,
        family_id: 11,
        updater_family_id: 35,
    }
}

fn archive(metadata: FirmwareMetadata, firmware: &[u8]) -> Vec<u8> {
    let mut zip = zip::ZipWriter::new(Cursor::new(Vec::new()));
    let options = zip::write::SimpleFileOptions::default()
        .compression_method(zip::CompressionMethod::Deflated);
    let mut manifest = Vec::new();
    plist::to_writer_xml(
        &mut manifest,
        &BTreeMap::from([("FirmwarePayload", metadata)]),
    )
    .unwrap();
    for (name, bytes) in [
        ("manifest.plist", manifest.as_slice()),
        ("Firmware-35.9.0.4", firmware),
    ] {
        zip.start_file(name, options).unwrap();
        zip.write_all(bytes).unwrap();
    }
    zip.finish().unwrap().into_inner()
}

#[test]
fn metadata_reports_version_and_rejects_wrong_families_and_builds() {
    assert_eq!(metadata().version(), "2.0.4");
    metadata().require_supported().unwrap();
    for change in 0..4 {
        let mut m = metadata();
        match change {
            0 => m.family_id = 1,
            1 => m.updater_family_id = 26,
            2 => m.visible_build_id = 0x02058000,
            _ => m.build_id = 0,
        }
        assert!(m.require_supported().is_err());
        assert!(Ipsw::parse(&archive(m, &[0; 32])).is_err());
    }
    assert!(Ipsw::parse(b"not a ZIP").is_err());
    assert!(Ipsw::parse(&archive(metadata(), &[0; 32])).is_err());
}

fn mse() -> Vec<u8> {
    let mut firmware = vec![0; 0x8000];
    firmware[0x100..0x10c].copy_from_slice(b"]ih[\0\x40\0\0\x0c\x01\x03\0");
    firmware[0x5000..0x5008].copy_from_slice(b"!ATAsoso");
    firmware[0x500c..0x5010].copy_from_slice(&0x6000u32.to_le_bytes());
    firmware[0x5010..0x5014].copy_from_slice(&0x1000u32.to_le_bytes());
    firmware[0x6000..0x6008].copy_from_slice(b"87021.0\x03");
    firmware[0x600c..0x6010].copy_from_slice(&24u32.to_le_bytes());
    firmware
}

#[test]
fn firmware_directory_bounds_and_final_aes_block() {
    let original = mse();
    assert_eq!(extract_osos(&original).unwrap().len(), 0x820);
    for (offset, value) in [
        (0x100, 0),
        (0x500c, 0xff),
        (0x500e, 0xff),
        (0x5013, 0xff),
        (0x6007, 2),
        (0x600f, 0xff),
    ] {
        let mut bad = original.clone();
        bad[offset] = value;
        assert!(extract_osos(&bad).is_err(), "{offset:x}");
    }
    let mut duplicate = original.clone();
    duplicate.copy_within(0x5000..0x5028, 0x5028);
    assert!(extract_osos(&duplicate).is_err());
    for n in [0, 0x527f, 0x6000, 0x6800] {
        assert!(extract_osos(&original[..n]).is_err());
    }
    assert!(Ipsw::parse(&archive(metadata(), &original))
        .err()
        .unwrap()
        .to_string()
        .contains("OSOS bytes"));
}

#[test]
#[ignore = "requires preserved local IPSW, NOR and decrypted Apple images"]
fn preserved_ipsw_and_nor_prepare_identical_assembly_inputs() {
    let root = std::path::PathBuf::from(
        std::env::var_os("REPRISE_ASSEMBLY_ROOT").expect("REPRISE_ASSEMBLY_ROOT"),
    );
    let ipsw_path = std::env::var_os("REPRISE_TEST_IPSW").expect("REPRISE_TEST_IPSW");
    let ipsw = Ipsw::load(Path::new(&ipsw_path)).unwrap();
    assert_eq!(ipsw.metadata.version(), "2.0.4");
    assert_eq!(ipsw.metadata.updater_family_id, 35);
    let nor = fs::read(root.join("inputs/nor.bin")).unwrap();
    let loader = fs::read(root.join("inputs/apple-loader.bin")).unwrap();
    let osos = fs::read(root.join("inputs/osos.bin")).unwrap();
    assert_eq!(ipsw.wrap_plaintext(&osos[0x800..]).unwrap(), osos);
    let original = AppleNorImage::locate(&nor).unwrap();
    assert!(original.encrypted());
    assert_eq!(original.offset, 0x8000);
    let prepared = PreparedInputs::from_plaintext(&ipsw, &nor, &osos, Some(&loader)).unwrap();
    let target: serde_json::Value =
        serde_json::from_slice(&fs::read(root.join("targets/classic7g-2.0.4.json")).unwrap())
            .unwrap();
    for (name, bytes) in &prepared.files {
        assert_eq!(
            bytes,
            &fs::read(root.join("inputs").join(name)).unwrap(),
            "{name}"
        );
        assert_eq!(
            sha256(bytes),
            target["inputs"][name]["sha256"].as_str().unwrap(),
            "{name}"
        );
    }
    let mut rockboxed = nor.clone();
    let offset = 0xa000;
    rockboxed[0x8000..0x8800].fill(0);
    rockboxed[0x8000..0x8008].copy_from_slice(b"87021.0\x02");
    rockboxed[0x800c..0x8010].copy_from_slice(&0x900u32.to_le_bytes());
    rockboxed[offset..offset + 0x800].copy_from_slice(&nor[0x8000..0x8800]);
    rockboxed[offset + 7] = 2;
    rockboxed[offset + 0x800..offset + 0x800 + loader.len()].copy_from_slice(&loader);
    let image = AppleNorImage::locate(&rockboxed).unwrap();
    assert_eq!(image.offset, offset);
    assert!(!image.encrypted());
    let from_modified = PreparedInputs::from_plaintext(&ipsw, &rockboxed, &osos, None).unwrap();
    assert_eq!(prepared.files, from_modified.files);
    rockboxed[offset + 0x900] ^= 1;
    assert!(AppleNorImage::locate(&rockboxed).is_err());
    let out = tempfile::tempdir().unwrap();
    let directory = out.path().join("inputs");
    prepared.write(&directory).unwrap();
    assert!(prepared.write(&directory).is_err());
    AppleInputs::load(&directory).unwrap();
}
