// SPDX-License-Identifier: GPL-3.0-only

use super::*;
use std::{
    fs,
    io::{Cursor, Write},
};

fn metadata() -> FirmwareMetadata {
    FirmwareMetadata {
        firmware_name: "Firmware-38.9.0.5".into(),
        build_id: 0x09058000,
        visible_build_id: 0x02058000,
        family_id: 11,
        updater_family_id: 38,
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
        ("Firmware-38.9.0.5", firmware),
    ] {
        zip.start_file(name, options).unwrap();
        zip.write_all(bytes).unwrap();
    }
    zip.finish().unwrap().into_inner()
}

#[test]
fn metadata_reports_version_and_rejects_wrong_families_and_builds() {
    assert_eq!(metadata().version(), "2.0.5");
    metadata().require_supported().unwrap();
    for change in 0..4 {
        let mut m = metadata();
        match change {
            0 => m.family_id = 1,
            1 => m.updater_family_id = 26,
            2 => m.visible_build_id = 0x02048000,
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
#[ignore = "requires preserved local IPSW and decrypted Apple images"]
fn preserved_ipsw_prepares_identical_assembly_inputs() {
    let root = std::path::PathBuf::from(
        std::env::var_os("REPRISE_ASSEMBLY_ROOT").expect("REPRISE_ASSEMBLY_ROOT"),
    );
    let inputs = root.join("inputs/firmware-2.0.5");
    let ipsw = Ipsw::load(&root.join("inputs/ipsw/iPod_38.2.0.5.ipsw")).unwrap();
    let osos = fs::read(inputs.join("osos.bin")).unwrap();
    let aupd = fs::read(inputs.join("aupd.decrypted.body.bin")).unwrap();
    assert_eq!(ipsw.wrap_plaintext(&osos[0x800..]).unwrap(), osos);
    let prepared = PreparedInputs::from_plaintext(&ipsw, &osos, &aupd).unwrap();
    for (name, bytes) in &prepared.files {
        assert_eq!(bytes, &fs::read(inputs.join(name)).unwrap(), "{name}");
    }
    let mut corrupt = aupd.clone();
    corrupt[0] ^= 1;
    assert!(PreparedInputs::from_plaintext(&ipsw, &osos, &corrupt).is_err());
    let mut corrupt = osos.clone();
    corrupt[0x800] ^= 1;
    assert!(PreparedInputs::from_plaintext(&ipsw, &corrupt, &aupd).is_err());
    let out = tempfile::tempdir().unwrap();
    let directory = out.path().join("inputs");
    prepared.write(&directory).unwrap();
    assert!(prepared.write(&directory).is_err());
    AppleInputs::load(&directory).unwrap();
}
