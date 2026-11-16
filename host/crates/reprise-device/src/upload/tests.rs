// SPDX-License-Identifier: GPL-3.0-only

use super::image::{relocate_cold_init, COLD_INIT_SIZE, COLD_INIT_TAG, ROM_END, ROM_START};
use super::protocol::{CONFIG_SIZE, NEXT};
use super::*;
use crate::firmware;
use crate::DeviceInfo;
use std::io::Cursor;

fn fixture_helper() -> Result<UploadHelper> {
    UploadHelper::from_bytes(
        include_bytes!("../../../../../usb-helper/tests/fixtures/upload.dfu"),
        include_bytes!("../../../../../usb-helper/tests/fixtures/manifest.json"),
    )
}

#[test]
fn helper_identity_preserves_legacy_and_rejects_apple_winusb_alias() {
    let legacy = fixture_helper().unwrap();
    assert_eq!(
        (legacy.usb.vendor_id, legacy.usb.product_id),
        (0x05ac, 0x1261)
    );
    assert_eq!(legacy.validate_platform().is_err(), cfg!(windows));
    let image = include_bytes!("../../../../../usb-helper/tests/fixtures/upload.dfu");
    let mut manifest: serde_json::Value = serde_json::from_slice(include_bytes!(
        "../../../../../usb-helper/tests/fixtures/manifest.json"
    ))
    .unwrap();
    manifest["usb"] =
        serde_json::json!({"vendor_id": 0xf055, "product_id": 0x180d, "winusb": true});
    let helper = UploadHelper::from_bytes(image, &serde_json::to_vec(&manifest).unwrap()).unwrap();
    assert_eq!(
        (helper.usb.vendor_id, helper.usb.product_id),
        (0xf055, 0x180d)
    );
    helper.validate_platform().unwrap();
    manifest["usb"]["vendor_id"] = serde_json::json!(0x05ac);
    assert!(UploadHelper::from_bytes(image, &serde_json::to_vec(&manifest).unwrap()).is_err());
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
    status_at(size, state, sha, "/osos-cfw.bin")
}

fn status_at(size: u32, state: u32, sha: &[u8; 32], path: &str) -> Status {
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
    raw[128..128 + path.len()].copy_from_slice(path.as_bytes());
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

    fn next(&mut self, _: &[u8; CONFIG_SIZE]) -> Result<()> {
        panic!("single-file transfer must not queue a next file")
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
            3,
            &mut |p| {
                progress.push(p);
                Ok(())
            },
        )
        .unwrap();
        result.complete(&hash, &options()).unwrap();
        assert_eq!(bulk.sent, size);
        assert_eq!(bulk.commands, vec![0x53]);
        assert!(progress.iter().all(|p| p.completed <= p.total));
        assert!(progress.iter().all(|p| p.stage == "upload" && p.file == 3));
    }
}

#[test]
fn transfer_rejects_short_usb_and_changed_input_and_reports_hash_failure() {
    let mut bulk = fake(512);
    bulk.short = true;
    assert!(transfer(
        &mut bulk,
        &mut Cursor::new(vec![1; 512]),
        &[7; 16],
        512,
        0,
        &mut |_| Ok(())
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
            0,
            &mut |_| Ok(())
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
        0,
        &mut |_| Ok(()),
    )
    .unwrap();
    assert!(result
        .complete(&Sha256::digest([1; 512]).into(), &options())
        .is_err());
    assert_eq!(bulk.commands, vec![0x53]);
}

/// Serves a batch: each file starts READY, completes once its bytes arrived and
/// moves on when NEXT names the following path.
struct BatchFake {
    files: Vec<(u32, &'static str)>,
    current: usize,
    started: bool,
    sent: u32,
    hash: Sha256,
    commands: Vec<u8>,
    payloads: Vec<[u8; CONFIG_SIZE]>,
    corrupt: Option<usize>,
    /// Report the previous file's byte count as received after NEXT.
    stale: bool,
    /// Lose the device while sending this file.
    unplug: Option<usize>,
}

impl BatchFake {
    fn new(files: &[(u32, &'static str)]) -> Self {
        Self {
            files: files.to_vec(),
            current: 0,
            started: false,
            sent: 0,
            hash: Sha256::new(),
            commands: vec![],
            payloads: vec![],
            corrupt: None,
            stale: false,
            unplug: None,
        }
    }
}

impl BulkTransport for BatchFake {
    fn endpoint(&self) -> u8 {
        2
    }

    fn command(&mut self, req: u8, nonce: &[u8; 16]) -> Result<()> {
        assert_eq!(nonce, &[7; 16]);
        if req == START {
            assert!(!self.started);
            self.started = true;
        } else {
            assert_eq!(req, ACKNOWLEDGE);
            assert_eq!(self.current + 1, self.files.len());
        }
        self.commands.push(req);
        Ok(())
    }

    fn next(&mut self, payload: &[u8; CONFIG_SIZE]) -> Result<()> {
        assert!(self.started && self.sent == self.files[self.current].0);
        assert_eq!(&payload[..16], &[7; 16]);
        self.commands.push(NEXT);
        self.payloads.push(*payload);
        self.current += 1;
        self.started = false;
        self.sent = 0;
        self.hash = Sha256::new();
        Ok(())
    }

    fn send(&mut self, b: &[u8]) -> Result<usize> {
        assert!(self.started);
        if self.unplug == Some(self.current) {
            return Err(crate::usb::usb_error("file upload", rusb::Error::NoDevice));
        }
        let n = (self.files[self.current].0 - self.sent).min(CHUNK as u32) as usize;
        self.hash.update(&b[..n]);
        self.sent += n as u32;
        Ok(b.len())
    }

    fn status(&mut self, nonce: &[u8; 16], size: u32) -> Result<Status> {
        assert_eq!(nonce, &[7; 16]);
        let (expected, path) = self.files[self.current];
        assert_eq!(size, expected);
        let state = if !self.started {
            READY
        } else if self.sent == size {
            COMPLETE
        } else {
            2
        };
        let mut result = status_at(size, state, &self.hash.clone().finalize().into(), path);
        if self.corrupt == Some(self.current) {
            result.raw[64] ^= 1;
        }
        if self.stale && self.current > 0 && state == READY {
            put(&mut result.raw, 36, 1);
        }
        Ok(result)
    }
}

fn device() -> DeviceInfo {
    DeviceInfo {
        selector: crate::DeviceSelector { bus: 1, address: 2 },
        port_path: vec![3],
        vendor_id: 0x05ac,
        product_id: 0x1227,
        mode: "test",
        dfu_candidate: true,
    }
}

fn batch(
    bulk: &mut BatchFake,
    emit: &mut Emit<'_>,
) -> (Result<Status>, Vec<UploadReport>, Vec<[u8; 32]>) {
    let data: Vec<Vec<u8>> = bulk
        .files
        .iter()
        .map(|(size, _)| (0..*size).map(|i| (i * 37) as u8).collect())
        .collect();
    let hashed: Vec<(u32, [u8; 32])> = data
        .iter()
        .map(|d| (d.len() as u32, Sha256::digest(d).into()))
        .collect();
    let mut cursors: Vec<Cursor<&[u8]>> = data.iter().map(|d| Cursor::new(d.as_slice())).collect();
    let mut files: Vec<UploadFile<'_>> = cursors
        .iter_mut()
        .zip(&bulk.files)
        .map(|(input, (_, path))| UploadFile {
            input,
            options: UploadOptions {
                destination: (*path).into(),
                overwrite: true,
            },
        })
        .collect();
    let mut reports = vec![];
    let result = transfer_files(
        bulk,
        &mut files,
        &hashed,
        &[7; 16],
        &device(),
        &mut reports,
        emit,
    );
    (result, reports, hashed.iter().map(|h| h.1).collect())
}

const BATCH: [(u32, &str); 3] = [
    (0, "/themes/default/theme.bin"),
    (513, "/cfw-loader.bin"),
    (70000, "/osos-cfw.bin"),
];

#[test]
fn batch_queues_each_further_file_with_next_and_acknowledges_once() {
    let mut bulk = BatchFake::new(&BATCH);
    let mut progress = vec![];
    let (result, reports, hashes) = batch(&mut bulk, &mut |p| {
        progress.push(p);
        Ok(())
    });
    let last = result.unwrap();
    assert_eq!(last.state(), COMPLETE);
    assert_eq!(
        bulk.commands,
        vec![START, NEXT, START, NEXT, START, ACKNOWLEDGE]
    );
    let destinations: Vec<&str> = reports.iter().map(|r| r.destination.as_str()).collect();
    assert_eq!(destinations, BATCH.map(|f| f.1));
    assert_eq!(reports[2].bytes, 70000);
    assert_eq!(reports[2].nonce, "07".repeat(16));
    for (payload, (index, (size, path))) in
        bulk.payloads.iter().zip(BATCH.iter().enumerate().skip(1))
    {
        assert_eq!(word(payload, 16), *size);
        assert_eq!(word(payload, 20), 1);
        assert_eq!(&payload[24..56], &hashes[index]);
        assert_eq!(&payload[56..56 + path.len()], path.as_bytes());
        assert!(payload[56 + path.len()..].iter().all(|b| *b == 0));
    }
    for stage in ["prepare", "complete"] {
        let files: Vec<usize> = progress
            .iter()
            .filter(|p| p.stage == stage)
            .map(|p| p.file)
            .collect();
        assert_eq!(files, vec![0, 1, 2], "{stage}");
    }
    assert!(progress
        .iter()
        .all(|p| p.file < 3 && p.completed <= p.total));
}

#[test]
fn batch_stops_at_the_first_bad_file_and_keeps_the_verified_reports() {
    let mut bulk = BatchFake::new(&BATCH);
    bulk.corrupt = Some(1);
    let (result, reports, _) = batch(&mut bulk, &mut |_| Ok(()));
    let error = result.unwrap_err().to_string();
    assert!(error.starts_with("/cfw-loader.bin: "), "{error}");
    assert_eq!(bulk.commands, vec![START, NEXT, START]);
    assert_eq!(reports.len(), 1);

    let mut bulk = BatchFake::new(&BATCH);
    bulk.stale = true;
    let (result, reports, _) = batch(&mut bulk, &mut |_| Ok(()));
    assert!(result.unwrap_err().to_string().contains("not ready"));
    assert_eq!(bulk.commands, vec![START, NEXT]);
    assert_eq!(reports.len(), 1);

    let mut bulk = BatchFake::new(&BATCH);
    let (result, reports, _) = batch(&mut bulk, &mut |p| {
        if p.file == 2 {
            Err(invalid("cancelled"))
        } else {
            Ok(())
        }
    });
    assert!(result.unwrap_err().to_string().contains("cancelled"));
    assert_eq!(bulk.commands, vec![START, NEXT, START]);
    assert_eq!(reports.len(), 2);
}

#[test]
fn a_lost_device_mid_file_reports_the_ipod_stopped() {
    let mut bulk = BatchFake::new(&BATCH);
    bulk.unplug = Some(2);
    let (result, reports, _) = batch(&mut bulk, &mut |_| Ok(()));
    let error = result.unwrap_err();
    assert!(error.to_string().starts_with("/osos-cfw.bin: "), "{error}");
    assert!(error.device_stopped());
    assert_eq!(reports.len(), 2);

    let mut bulk = BatchFake::new(&BATCH);
    bulk.corrupt = Some(1);
    let (result, _, _) = batch(&mut bulk, &mut |_| Ok(()));
    assert!(!result.unwrap_err().device_stopped());
}

#[test]
fn helpers_without_batch_upload_take_one_file() {
    assert!(check_file_count(1, false).is_ok());
    assert!(check_file_count(2, false).is_err());
    assert!(check_file_count(2, true).is_ok());
    assert!(check_file_count(0, true).is_err());
}

#[test]
fn batch_upload_is_a_manifest_capability() {
    assert!(!fixture_helper().unwrap().supports_batch_upload());
    let image = include_bytes!("../../../../../usb-helper/tests/fixtures/upload.dfu");
    let mut manifest: serde_json::Value = serde_json::from_slice(include_bytes!(
        "../../../../../usb-helper/tests/fixtures/manifest.json"
    ))
    .unwrap();
    manifest["batch_upload"] = true.into();
    let helper = UploadHelper::from_bytes(image, &serde_json::to_vec(&manifest).unwrap()).unwrap();
    assert!(helper.supports_batch_upload());
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

struct InspectionFake {
    raw: Vec<u8>,
    commands: Vec<u8>,
    save_sectors: Option<bool>,
}

impl BulkTransport for InspectionFake {
    fn diagnostics(&mut self, directory: Option<&std::path::Path>, log: &mut dyn FnMut(String)) {
        assert!(self.commands.is_empty());
        self.save_sectors = Some(directory.is_some());
        log("Storage diagnostics unavailable: simulated failure".into());
    }
    fn endpoint(&self) -> u8 {
        2
    }
    fn status(&mut self, nonce: &[u8; 16], size: u32) -> Result<Status> {
        Status::parse(&self.raw, nonce, size)
    }
    fn command(&mut self, request: u8, _: &[u8; 16]) -> Result<()> {
        self.commands.push(request);
        Ok(())
    }
    fn send(&mut self, _: &[u8]) -> Result<usize> {
        panic!("Storage inspection must not upload file data")
    }
    fn next(&mut self, _: &[u8; CONFIG_SIZE]) -> Result<()> {
        panic!("Storage inspection has no next file")
    }
}

#[test]
fn layout_failure_is_preserved_and_old_zero_size_status_is_cancelled() {
    for reported_size in [512, 0] {
        let mut raw = status(reported_size, COMPLETE, &[0; 32]).raw;
        put(&mut raw, 28, (-301i32) as u32);
        for offset in [36, 40, 44] {
            put(&mut raw, offset, 0);
        }
        let mut bulk = InspectionFake {
            raw,
            commands: vec![],
            save_sectors: None,
        };
        let mut logs = vec![];
        let error = inspect_bulk(
            &mut bulk,
            &[7; 16],
            512,
            Some(std::path::Path::new("diagnostics")),
            &mut |s| logs.push(s),
        )
        .unwrap_err()
        .to_string();
        assert_eq!(bulk.save_sectors, Some(true));
        assert!(error.contains("code -301"), "{error}");
        assert_eq!(
            logs[0],
            "Storage diagnostics unavailable: simulated failure"
        );
        if reported_size == 0 {
            assert!(error.contains("size: expected 512, received 0"));
            assert_eq!(bulk.commands, [CANCEL]);
            assert!(logs.iter().any(|s| s.contains("cancellation sent")));
        } else {
            assert_eq!(bulk.commands, [ACKNOWLEDGE]);
            assert!(logs.iter().any(|s| s.contains("rc=-301")));
        }
    }
}

#[test]
fn mac_format_error_reaches_the_caller_and_preserves_diagnostics() {
    let mut raw = status(512, COMPLETE, &[0; 32]).raw;
    put(&mut raw, 28, (-323i32) as u32);
    for offset in [36, 40, 44] {
        put(&mut raw, offset, 0);
    }
    let mut bulk = InspectionFake {
        raw,
        commands: vec![],
        save_sectors: None,
    };
    let error = inspect_bulk(
        &mut bulk,
        &[7; 16],
        512,
        Some(std::path::Path::new("diagnostics")),
        &mut |_| {},
    )
    .unwrap_err()
    .to_string();
    assert!(error.contains("Apple Partition Map"), "{error}");
    assert!(
        error.contains("FAT32 with Master Boot Record (MBR)"),
        "{error}"
    );
    assert!(error.contains("HFS/HFS+ is not supported"), "{error}");
    assert_eq!(bulk.commands, [ACKNOWLEDGE]);
    assert_eq!(bulk.save_sectors, Some(true));

    let mut diagnostics = vec![0; diagnostics::DIAGNOSTICS_SIZE];
    put(&mut diagnostics, 0, 0x53544731);
    put(&mut diagnostics, 4, 1);
    put(&mut diagnostics, 24, 27);
    let mut logs = vec![];
    diagnostics::log_storage(&diagnostics, &mut |s| logs.push(s)).unwrap();
    assert!(logs
        .last()
        .unwrap()
        .contains("Apple Partition Map detected"));
}

#[test]
fn storage_inspection_keeps_logs_but_saves_sectors_only_on_failure() {
    for state in [COMPLETE, READY] {
        let mut bulk = InspectionFake {
            raw: status(512, state, &[0; 32]).raw,
            commands: vec![],
            save_sectors: None,
        };
        let mut logs = vec![];
        let result = inspect_bulk(
            &mut bulk,
            &[7; 16],
            512,
            Some(std::path::Path::new("diagnostics")),
            &mut |s| logs.push(s),
        );
        assert_eq!(result.is_ok(), state == COMPLETE);
        assert_eq!(bulk.save_sectors, Some(state != COMPLETE));
        assert!(logs.iter().any(|s| s.contains("Storage diagnostics")));
        assert_eq!(
            bulk.commands,
            [if state == COMPLETE {
                ACKNOWLEDGE
            } else {
                CANCEL
            }]
        );
    }
}

#[test]
fn status_mismatch_names_the_field_without_trusting_another_sessions_error() {
    let mut raw = status(512, COMPLETE, &[0; 32]).raw;
    put(&mut raw, 28, (-301i32) as u32);
    let error = Status::parse(&raw, &[8; 16], 512).unwrap_err().to_string();
    assert!(error.contains("nonce does not match"));
    assert!(!error.contains("Disk layout"));
    put(&mut raw, 48, 0);
    let error = Status::parse(&raw, &[7; 16], 512).unwrap_err().to_string();
    assert!(error.contains("endpoint: 0 outside 1..=15"));
}

#[test]
fn storage_diagnostics_report_capacity_partition_geometry_and_rejection() {
    let mut raw = vec![0; diagnostics::DIAGNOSTICS_SIZE];
    for (offset, value) in [
        (0, 0x53544731),
        (4, 1),
        (8, 512),
        (12, 1000000),
        (24, 8),
        (28, 1),
        (32, 3),
        (56, 0x0c),
        (60, 2048),
        (64, 2000000),
        (72, 512),
    ] {
        put(&mut raw, offset, value);
    }
    let mut logs = vec![];
    diagnostics::log_storage(&raw, &mut |s| logs.push(s)).unwrap();
    assert!(logs.iter().any(|s| s.contains("1000000 sectors")));
    assert!(logs
        .iter()
        .any(|s| s.contains("type=0x0c; start=2048; length=2000000")));
    assert!(logs
        .iter()
        .any(|s| s.contains("extends beyond disk capacity")));
    assert!(diagnostics::log_storage(&raw[..100], &mut |_| {}).is_err());
    put(&mut raw, 4, 2);
    assert!(diagnostics::log_storage(&raw, &mut |_| {}).is_err());
}

#[test]
fn storage_diagnostics_use_partition_address_units_for_bounds() {
    let mut raw = vec![0; diagnostics::DIAGNOSTICS_SIZE];
    for (offset, value) in [
        (0, 0x53544731),
        (4, 1),
        (8, 512),
        (12, 20000000),
        (56, 0x0c),
        (60, 2048),
        (64, 2000000),
    ] {
        put(&mut raw, offset, value);
    }
    for (scale, fat_bytes) in [(1, 4096), (8, 512)] {
        put(&mut raw, 68, 2048 * scale);
        put(&mut raw, 72, fat_bytes);
        let mut logs = vec![];
        diagnostics::log_storage(&raw, &mut |s| logs.push(s)).unwrap();
        assert!(logs.iter().any(|s| s.contains(&format!(
            "bounds in 512-byte sectors: start={}; end={}; disk capacity=20000000",
            2048 * scale,
            2002048 * scale
        ))));
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
    let m = serde_json::json!({"schema":3,"mode":"stream-file","rom_sha256":firmware::current().compatibility.bootrom_sha256,
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

#[test]
fn storage_cleanup_failure_keeps_its_wording_and_both_errors() {
    let error = cleanup_after(
        invalid("Storage probe failed"),
        Error::TimedOut("DFU return timed out; re-enter DFU before another upload".into()),
    );
    assert_eq!(
        error.to_string(),
        "Storage probe failed; DFU cleanup failed: DFU return timed out; re-enter DFU before another upload"
    );
    assert!(error.device_stopped());
    let lost_step = cleanup_after(
        crate::usb::usb_error("storage probe", rusb::Error::NoDevice),
        invalid("Returned device is not DFU idle"),
    );
    assert!(lost_step.device_stopped());
}
