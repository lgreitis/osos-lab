// SPDX-License-Identifier: GPL-3.0-only

use super::*;
use std::io::Cursor;

fn fixture_helper() -> Result<UploadHelper> {
    UploadHelper::from_bytes(
        include_bytes!("../../../../../usb-helper/tests/fixtures/upload.dfu"),
        include_bytes!("../../../../../usb-helper/tests/fixtures/manifest.json"),
    )
}

fn put(b: &mut [u8], offset: usize, value: u32) {
    b[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

fn options() -> UploadOptions {
    UploadOptions {
        destination: "/osos-cfw.bin".into(),
        overwrite: true,
    }
}

fn status(size: u32, state: u32, sha: &[u8; 32]) -> Status {
    let mut raw = vec![0; RESULT_SIZE];
    for (at, v) in [
        (0, 0x55504c32),
        (4, 2),
        (24, state),
        (32, size),
        (48, 2),
        (52, 1),
    ] {
        put(&mut raw, at, v);
    }
    raw[8..24].fill(7);
    if state == 4 {
        for at in [36, 40, 44] {
            put(&mut raw, at, size);
        }
    }
    raw[64..96].copy_from_slice(sha);
    raw[96..128].copy_from_slice(sha);
    raw[128..141].copy_from_slice(b"/osos-cfw.bin");
    Status { raw }
}
struct Fake {
    size: u32,
    sent: u32,
    commands: Vec<u8>,
    hash: Sha256,
    short: bool,
    corrupt: bool,
}

impl BulkTransport for Fake {
    fn endpoint(&self) -> u8 {
        2
    }

    fn command(&mut self, req: u8, nonce: &[u8; 16]) -> Result<()> {
        assert_eq!(nonce, &[7; 16]);
        self.commands.push(req);
        Ok(())
    }

    fn send(&mut self, b: &[u8]) -> Result<usize> {
        assert_eq!(self.commands, vec![0x53]);
        assert!(b.len() <= CHUNK && b.len().is_multiple_of(512));
        let n = (self.size - self.sent).min(CHUNK as u32) as usize;
        assert!(b[n..].iter().all(|v| *v == 0));
        self.hash.update(&b[..n]);
        self.sent += n as u32;
        Ok(b.len() - usize::from(self.short))
    }

    fn status(&mut self, nonce: &[u8; 16], size: u32) -> Result<Status> {
        assert_eq!(size, self.size);
        assert_eq!(nonce, &[7; 16]);
        let mut result = status(
            size,
            if self.commands.is_empty() { 1 } else { 4 },
            &self.hash.clone().finalize().into(),
        );
        if self.corrupt {
            result.raw[64] ^= 1;
        }
        Ok(result)
    }
}

fn fake(size: u32) -> Fake {
    Fake {
        size,
        sent: 0,
        commands: vec![],
        hash: Sha256::new(),
        short: false,
        corrupt: false,
    }
}

#[test]
fn streaming_exceeds_old_limit_and_preserves_odd_lengths() {
    for size in [
        0,
        1,
        511,
        512,
        513,
        65535,
        65536,
        65537,
        13 * 1024 * 1024 + 17,
    ] {
        let data: Vec<u8> = (0..size).map(|i| (i * 37) as u8).collect();
        let hash: [u8; 32] = Sha256::digest(&data).into();
        let mut bulk = fake(size);
        let mut progress = vec![];
        let result = transfer(
            &mut bulk,
            &mut Cursor::new(&data),
            &[7; 16],
            size,
            &mut |p| progress.push(p),
        )
        .unwrap();
        result.complete(&hash, &options()).unwrap();
        assert_eq!(bulk.sent, size);
        assert_eq!(bulk.commands, vec![0x53, 0x54]);
        assert!(progress.iter().all(|p| p.completed <= p.total));
        assert!(progress.iter().all(|p| p.stage == "upload"));
    }
}

#[test]
fn transfer_rejects_short_usb_and_changed_input_but_acknowledges_hash_failure() {
    let mut bulk = fake(512);
    bulk.short = true;
    assert!(transfer(
        &mut bulk,
        &mut Cursor::new(vec![1; 512]),
        &[7; 16],
        512,
        &mut |_| {}
    )
    .is_err());
    assert_eq!(bulk.commands, vec![0x53]);
    for bytes in [511, 513] {
        let mut bulk = fake(512);
        assert!(transfer(
            &mut bulk,
            &mut Cursor::new(vec![1; bytes]),
            &[7; 16],
            512,
            &mut |_| {}
        )
        .is_err());
        assert_eq!(bulk.commands, vec![0x53]);
    }
    let mut bulk = fake(512);
    bulk.corrupt = true;
    let result = transfer(
        &mut bulk,
        &mut Cursor::new(vec![1; 512]),
        &[7; 16],
        512,
        &mut |_| {},
    )
    .unwrap();
    assert!(result
        .complete(&Sha256::digest([1; 512]).into(), &options())
        .is_err());
    assert_eq!(bulk.commands, vec![0x53, 0x54]);
}

#[test]
fn result_requires_nonce_counts_path_both_hashes_and_clean_disk_operations() {
    let original = status(512, 4, &[9; 32]);
    let parsed = Status::parse(&original.raw, &[7; 16], 512).unwrap();
    parsed.complete(&[9; 32], &options()).unwrap();
    for at in [
        0, 4, 8, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 96, 128, 319,
    ] {
        let mut bad = original.raw.clone();
        bad[at] ^= 0x80;
        assert!(
            Status::parse(&bad, &[7; 16], 512)
                .and_then(|r| r.complete(&[9; 32], &options()))
                .is_err(),
            "{at}"
        );
    }
    for n in 0..RESULT_SIZE {
        assert!(Status::parse(&original.raw[..n], &[7; 16], 512).is_err());
    }
}

#[test]
fn helper_validation_rejects_old_images_and_invalid_slots() {
    let mut image = vec![0; 0xe00];
    image[..8].copy_from_slice(b"87021.0\x02");
    put(&mut image, 12, 0x600);
    image[0x800..0x810].copy_from_slice(b"REPRISE-NONCE-01");
    image[0x810..0x820].copy_from_slice(b"REPRISE-UPLOAD2\0");
    image[0xa00..0xa10].copy_from_slice(COLD_INIT_TAG);
    let m = serde_json::json!({"schema":3,"mode":"stream-file","rom_sha256":SUPPORTED_BOOTROM_SHA256,
        "bytes":image.len(),"sha256":format!("{:x}",Sha256::digest(&image)),
        "nonce_offset":0x800,"config_offset":0x810,"cold_init_offset":0xa00});
    let helper = UploadHelper::from_bytes(&image, &serde_json::to_vec(&m).unwrap()).unwrap();
    for rom in [vec![], vec![0; crate::BOOTROM_SIZE]] {
        assert!(helper
            .prepare(&rom, &[7; 16], 1, &[9; 32], &options())
            .is_err());
    }
    for key in ["nonce_offset", "config_offset", "cold_init_offset"] {
        for offset in [0, 0x801, 0x900, usize::MAX] {
            let mut bad = m.clone();
            bad[key] = offset.into();
            assert!(UploadHelper::from_bytes(&image, &serde_json::to_vec(&bad).unwrap()).is_err());
        }
    }
    for offset in [0xa10, 0xdff] {
        let mut bad_image = image.clone();
        bad_image[offset] = 1;
        let mut bad = m.clone();
        bad["sha256"] = format!("{:x}", Sha256::digest(&bad_image)).into();
        assert!(UploadHelper::from_bytes(&bad_image, &serde_json::to_vec(&bad).unwrap()).is_err());
    }
    let mut old = m;
    old["schema"] = 2.into();
    assert!(UploadHelper::from_bytes(&image, &serde_json::to_vec(&old).unwrap()).is_err());
    assert!(UploadHelper::from_bytes(&[], b"{}").is_err());
}

#[test]
fn reset_relocation_preserves_branches_and_skips_literal_pool() {
    let mut rom = vec![0; crate::BOOTROM_SIZE];
    // Both branch directions, the continuation, and the last possible literal load.
    put(&mut rom, ROM_START, 0xea000000); // forward to +8
    put(&mut rom, ROM_START + 4, 0xea00006e); // continuation at ROM_END
    put(&mut rom, ROM_START + 8, 0x1afffffc); // back to ROM_START
    put(&mut rom, ROM_END - 4, 0xe59f3000);
    put(&mut rom, ROM_END + 4, 0x12345678);
    let relocated = relocate_cold_init(&rom).unwrap();
    for pos in [0, 4, 8] {
        assert_eq!(word(&relocated, pos), word(&rom, ROM_START + pos));
    }
    let code_size = ROM_END - ROM_START;
    let load = word(&relocated, code_size - 4);
    assert_eq!(load & !0xfff, 0xe59f3000);
    assert_eq!(
        word(&relocated, code_size + 4 + (load & 0xfff) as usize),
        0x12345678
    );
    let branch = word(&relocated, code_size);
    assert_eq!(branch >> 24, 0xea);
    assert_eq!(
        code_size + 8 + ((branch & 0xffffff) as usize * 4),
        COLD_INIT_SIZE
    );
    for branch in [0xeafffffd, 0xea000070] {
        put(&mut rom, ROM_START, branch);
        assert!(relocate_cold_init(&rom).is_err());
    }
}

#[test]
fn fixture_helper_contains_a_pristine_rom_slot() {
    let helper = fixture_helper().unwrap();
    assert_eq!(word(&helper.image, helper.cold_init_offset), 0xeafffffe);
    assert!(
        helper.image[helper.cold_init_offset + 16..helper.cold_init_offset + COLD_INIT_SIZE]
            .iter()
            .all(|b| *b == 0)
    );
}

#[test]
fn paths_reject_traversal_aliases_and_reserved_temporary_names() {
    for path in [
        "",
        "os.bin",
        "/",
        "/../os",
        "/./os",
        "/a//b",
        "/os.",
        "/os ",
        "/a\\b",
        "/a:b",
        "/.reprise-test.part",
        "/é.bin",
    ] {
        assert!(
            UploadOptions {
                destination: path.into(),
                overwrite: false
            }
            .validate()
            .is_err(),
            "{path}"
        );
    }
    for path in ["/osos-cfw.bin", "/folder/os patched.bin"] {
        assert!(UploadOptions {
            destination: path.into(),
            overwrite: true
        }
        .validate()
        .is_ok());
    }
}

#[test]
#[ignore = "Requires REPRISE_TEST_BOOTROM; reads saved files only"]
fn saved_rom_prepares_fixture_helper() {
    let rom = std::fs::read(std::env::var("REPRISE_TEST_BOOTROM").unwrap()).unwrap();
    let helper = fixture_helper().unwrap();
    let patched = helper
        .prepare(&rom, &[7; 16], 13 * 1024 * 1024 + 17, &[9; 32], &options())
        .unwrap();
    assert_eq!(
        word(&patched, helper.config_offset + 16),
        13 * 1024 * 1024 + 17
    );
    assert_eq!(word(&patched, helper.config_offset + 20), 1);
    assert_eq!(
        &patched[helper.config_offset + 24..helper.config_offset + 56],
        &[9; 32]
    );
    assert_eq!(
        &patched[helper.config_offset + 56..helper.config_offset + 69],
        b"/osos-cfw.bin"
    );
    assert_eq!(
        &patched[helper.nonce_offset..helper.nonce_offset + 16],
        &[7; 16]
    );
    for at in (ROM_START..ROM_END).step_by(4) {
        let original = word(&rom, at);
        let pos = helper.cold_init_offset + at - ROM_START;
        let relocated = word(&patched, pos);
        if original & 0xffff0000 == 0xe59f0000 {
            assert_eq!(original & !0xfff, relocated & !0xfff);
            assert_eq!(
                word(&rom, at + 8 + (original & 0xfff) as usize),
                word(&patched, pos + 8 + (relocated & 0xfff) as usize)
            );
        } else {
            assert_eq!(original, relocated, "instruction at {at:#x}");
        }
    }
    for (at, (before, after)) in helper.image.iter().zip(&patched).enumerate() {
        if ![
            (helper.nonce_offset, 16),
            (helper.config_offset, 248),
            (helper.cold_init_offset, COLD_INIT_SIZE),
        ]
        .iter()
        .any(|(start, len)| (*start..start + len).contains(&at))
        {
            assert_eq!(before, after, "unexpected change at {at:#x}");
        }
    }
    let mut corrupt = rom;
    corrupt[0] ^= 1;
    assert!(helper
        .prepare(&corrupt, &[7; 16], 1, &[9; 32], &options())
        .is_err());
}
