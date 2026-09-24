// SPDX-License-Identifier: GPL-3.0-only

use crate::{
    dfu::{Dfu, Transport},
    *,
};
use std::collections::VecDeque;

pub(crate) fn fixture() -> Vec<u8> {
    let mut bytes: Vec<_> = [0x53436667u32, 144, 0x2000, 0x10001, 0, 6]
        .into_iter()
        .flat_map(u32::to_le_bytes)
        .collect();
    for (tag, data) in [
        ("Mod#", b"MC293".as_slice()),
        ("SrNm", b"TESTSERIAL"),
        ("SwVr", b"2.0.4"),
        ("HwVr", &[0, 0, 0, 0, 0, 2, 0x13, 0]),
        ("HwId", &[0x72, 0x43, 2, 0x82]),
        ("New!", &[1, 2, 3]),
    ] {
        bytes.extend(tag.bytes().rev());
        bytes.extend_from_slice(data);
        bytes.resize(bytes.len() + 16 - data.len(), 0);
    }
    bytes
}

#[test]
fn syscfg_parses_identity_and_preserves_unknown_records() {
    let mut bytes = fixture();
    bytes.extend_from_slice(&[0xff; 128]); // trailing NOR padding
    let cfg = SysCfg::parse(&bytes).unwrap();
    assert_eq!(cfg.size, 144);
    assert_eq!(&cfg.entries["New!"][..3], &[1, 2, 3]);
    let identity = cfg.identity().unwrap();
    assert_eq!(identity.model, "MC293");
    assert_eq!(identity.serial, "TESTSERIAL");
    assert_eq!(identity.hardware_version, 0x00130200);
    assert_eq!(identity.recorded_firmware, "2.0.4");
}

#[test]
fn syscfg_rejects_malformed_bounds_duplicates_and_text() {
    let original = fixture();
    for length in 0..original.len() {
        assert!(SysCfg::parse(&original[..length]).is_err());
    }
    for offset in [0, 4, 8, 12, 16, 20] {
        let mut bytes = original.clone();
        bytes[offset..offset + 4].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(SysCfg::parse(&bytes).is_err(), "offset {offset}");
    }
    let mut duplicate = original.clone();
    duplicate[44..48].copy_from_slice(&original[24..28]);
    assert!(SysCfg::parse(&duplicate).is_err());
    let mut garbage = original.clone();
    garbage[28] = 0xff;
    assert!(SysCfg::parse(&garbage).unwrap().identity().is_err());
    let mut embedded_nul = original.clone();
    embedded_nul[29] = 0;
    assert!(SysCfg::parse(&embedded_nul).unwrap().identity().is_err());
    let mut cfg = SysCfg::parse(&original).unwrap();
    cfg.entries.remove("HwVr");
    assert!(cfg.identity().is_err());
}

#[test]
fn compatibility_requires_model_hardware_and_recorded_version() {
    for (model, hw, version, compatible) in [
        ("MC293", 0x00130200u32, "2.0.4", true),
        ("MC297", 0x00130200, "2.0.4", true),
        ("MD717", 0x00130200, "2.0.4", false),
        ("MC293", 0x00130100, "2.0.4", false),
        ("MC293", 0x00130200, "2.0.5", false),
    ] {
        let mut cfg = SysCfg::parse(&fixture()).unwrap();
        for (tag, text) in [("Mod#", model), ("SwVr", version)] {
            let value = cfg.entries.get_mut(tag).unwrap();
            value.fill(0);
            value[..text.len()].copy_from_slice(text.as_bytes());
        }
        cfg.entries.get_mut("HwVr").unwrap()[4..8].copy_from_slice(&hw.to_le_bytes());
        let mut report = CheckReport::new("saved_files", None);
        report.record_identity(&cfg, &mut |_| {}).unwrap();
        assert_eq!(report.compatible, compatible);
    }
}

#[test]
fn offline_unknown_rom_never_passes_identity() {
    let report = check_saved(&vec![0; BOOTROM_SIZE], &fixture());
    assert!(!report.compatible);
    assert!(report.identity.is_none());
    assert_eq!(report.cleanup, Cleanup::NotAttempted);
    assert_eq!(report.checks[3].status, CheckStatus::Failed);
    assert_eq!(report.checks[4].status, CheckStatus::Skipped);
    assert_eq!(check_saved(&[0; 64], &fixture()).bootrom_sha256, None);
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

#[test]
fn payloads_match_independent_wind3x_assembler_output() {
    // Golden instruction bytes from wInd3x: offsets, boundaries, and NOR error branch.
    for (actual, expected) in [
        (payload::memory_read(0x20000000), "18e09fe53eff2fe114009fe54010a0e310209fe532ff2fe10ce09fe51eff2fe1180400200000002040aa0020d4480020"),
        (payload::memory_read(0x2000ffc0), "18e09fe53eff2fe114009fe54010a0e310209fe532ff2fe10ce09fe51eff2fe118040020c0ff002040aa0020d4480020"),
        (payload::nor_init(), "30e09fe53eff2fe10000a0e328e09fe53eff2fe12200a0e320e09fe53eff2fe11c009fe54010a0e318209fe532ff2fe114e09fe51eff2fe118040020241900207c1400200000002040aa0020d4480020"),
        (payload::nor_read(0, 60).unwrap(), "0000a0e3c810a0e35ce09fe53eff2fe10000a0e354e09fe53eff2fe1000050e30900001a0000a0e34010a0e338e09fe53eff2fe10000a0e30010a0e33c20a0e32c309fe52ce09fe53eff2fe128109fe5000081e520009fe54010a0e31c209fe532ff2fe118e09fe51eff2fe1908f00207c4c0020040002225c4e00200000022240aa0020d4480020"),
        (payload::nor_read(1, 59).unwrap(), "0000a0e3c810a0e35ce09fe53eff2fe10000a0e354e09fe53eff2fe1000050e30900001a0000a0e33f10a0e338e09fe53eff2fe10000a0e30110a0e33b20a0e32c309fe52ce09fe53eff2fe128109fe5000081e520009fe54010a0e31c209fe532ff2fe118e09fe51eff2fe1908f00207c4c0020040002225c4e00200000022240aa0020d4480020"),
        (payload::nor_read(0xfffff, 1).unwrap(), "0000a0e3c810a0e35ce09fe53eff2fe10000a0e354e09fe53eff2fe1000050e30900001a0000a0e30510a0e338e09fe53eff2fe10000a0e334109fe50120a0e330309fe530e09fe53eff2fe12c109fe5000081e524009fe54010a0e320209fe532ff2fe11ce09fe51eff2fe1908f00207c4c0020ffff0f00040002225c4e00200000022240aa0020d4480020"),
    ] { assert_eq!(hex(&actual), expected); }
    for (offset, size) in [(0, 0), (0, 61), (0xfffff, 2), (u32::MAX, 1)] {
        assert!(payload::nor_read(offset, size).is_err());
    }
}

struct Step {
    setup: (u8, u8, u16, u16),
    data: Vec<u8>,
    count: usize,
}

impl Script {
    fn rom_preflight(&mut self, rom: &[u8]) {
        for (start, size) in dfu::FAST_ROM_WINDOWS {
            for offset in (start..start + size).step_by(64) {
                self.probe(
                    &payload::memory_read(0x20000000 + offset as u32),
                    rom[offset..offset + 64].try_into().unwrap(),
                );
            }
        }
    }

    fn fast_rom(&mut self, rom: &[u8]) {
        assert_eq!(rom.len(), BOOTROM_SIZE);
        for (i, block) in rom.chunks_exact(512).enumerate() {
            self.clean();
            let mut wire = vec![0; payload::PREFIX_SIZE];
            wire.extend_from_slice(&payload::fast_bootrom(i as u32 * 512).unwrap());
            self.push((0x21, 1, 0, 0), &wire);
            self.clean();
            self.push((0xa1, 2, 0, 0), &[0; 64]);
            self.push((0xa0, 0xfe, 0xeaff, 6), block);
        }
    }
}

#[test]
fn fast_bootrom_payloads_match_independent_go_assembler() {
    use sha2::{Digest, Sha256};
    let mut hash = Sha256::new();
    for offset in (0..BOOTROM_SIZE as u32).step_by(512) {
        hash.update(payload::fast_bootrom(offset).unwrap());
    }

    // All 128 payloads assembled independently with wInd3x's Go assembler.
    assert_eq!(
        format!("{:x}", hash.finalize()),
        "12347a761232c7bbe74bbd63df08ca917eb39eba9529518925196465018841c5"
    );
    for offset in [1, 0xfe01, 0x10000, u32::MAX] {
        assert!(payload::fast_bootrom(offset).is_err());
    }
}

#[test]
#[ignore = "Requires REPRISE_TEST_BOOTROM saved file; never uses USB"]
fn saved_rom_fast_failures_stop_before_nor_and_preserve_cleanup() {
    let rom = std::fs::read(std::env::var("REPRISE_TEST_BOOTROM").unwrap()).unwrap();
    for fault in ["echo", "short", "timeout", "hash"] {
        let mut script = Script::default();
        script.state(2);
        script.rom_preflight(&rom);
        let echo_start = script.steps.len();
        script.echo_sweep();
        let read_start = script.steps.len();
        script.fast_rom(&rom);
        match fault {
            "echo" => {
                script.steps[echo_start + 6].data[16] ^= 1;
                script.steps.truncate(echo_start + 7);
            }
            "short" | "timeout" => {
                script.steps[read_start + 6].count =
                    if fault == "short" { 511 } else { usize::MAX };
                script.steps.truncate(read_start + 7);
            }
            "hash" => script.steps[read_start + 6].data[0] ^= 1,
            _ => unreachable!(),
        }
        script.idle_cleanup();
        let mut dfu = Dfu { transport: script };
        let (report, retained_rom) = usb::run_checks(&mut dfu, info(), |_| {});
        assert_eq!(
            retained_rom.is_some(),
            report.compatible && report.cleanup == Cleanup::Idle
        );
        assert!(!report.compatible, "{fault}");
        assert!(report.identity.is_none());
        assert_eq!(report.checks[3].status, CheckStatus::Failed);
        assert_eq!(report.cleanup, Cleanup::Idle);
        assert!(dfu.transport.steps.is_empty(), "{fault}");
    }
}

#[derive(Default)]
struct Script {
    steps: VecDeque<Step>,
}

impl Script {
    fn push(&mut self, setup: (u8, u8, u16, u16), data: &[u8]) {
        self.steps.push_back(Step {
            setup,
            data: data.to_vec(),
            count: data.len(),
        });
    }

    fn state(&mut self, state: u8) {
        self.push((0xa1, 5, 0, 0), &[state]);
    }

    fn command(&mut self, request: u8) {
        self.push((0x21, request, 0, 0), &[]);
    }

    fn clean(&mut self) {
        self.command(4);
        self.state(2);
    }

    fn probe(&mut self, payload: &[u8], response: &[u8; 64]) {
        self.clean();
        let mut download = vec![b'Z'; payload::PREFIX_SIZE];
        download.extend_from_slice(payload);
        self.push((0x21, 1, 0, 0), &download);
        self.clean();
        self.push((0xa1, 2, 0, 0), &[0; 64]);
        self.push((0xa0, 0xfe, 0xeaff, 6), response);
    }

    fn nor(&mut self, bytes: &[u8], offset: usize, count: usize, chunk: usize) {
        for start in (offset..offset + count).step_by(chunk) {
            let size = chunk.min(offset + count - start);
            let mut response = [0; 64];
            response[4..4 + size].copy_from_slice(&bytes[start..start + size]);
            self.probe(
                &payload::nor_read(start as u32, size as u32).unwrap(),
                &response,
            );
        }
    }
}

impl Transport for Script {
    fn read(&mut self, ty: u8, req: u8, val: u16, idx: u16, data: &mut [u8]) -> Result<usize> {
        let step = self.steps.pop_front().expect("Unexpected USB read");
        assert_eq!(step.setup, (ty, req, val, idx));
        assert_eq!(step.data.len(), data.len());
        if step.count == usize::MAX {
            return Err(Error::Usb {
                operation: "simulated read",
                source: rusb::Error::Timeout,
            });
        }
        data.copy_from_slice(&step.data);
        Ok(step.count)
    }

    fn write(&mut self, ty: u8, req: u8, val: u16, idx: u16, data: &[u8]) -> Result<usize> {
        let step = self.steps.pop_front().expect("Unexpected USB write");
        assert_eq!(step.setup, (ty, req, val, idx));
        assert_eq!(step.data, data);
        Ok(step.count)
    }
}

fn fast_fixture() -> (Vec<u8>, Vec<u8>) {
    // Generated with Go's crypto/aes + CBC, key "offline-test-key", IV 0x71*16.
    let fixture: serde_json::Value =
        serde_json::from_str(include_str!("testdata/fast-aes.json")).unwrap();
    let text = fixture["ciphertext"].as_str().unwrap();
    let cipher = (0..text.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&text[i..i + 2], 16).unwrap())
        .collect::<Vec<_>>();
    let plain = (0..cipher.len()).map(|i| (i * 37 + i / 19) as u8).collect();
    (cipher, plain)
}

#[test]
fn fast_payloads_match_go_for_every_chunk_size() {
    use sha2::{Digest, Sha256};
    let fixture: serde_json::Value =
        serde_json::from_str(include_str!("testdata/fast-aes.json")).unwrap();
    let mut hash = Sha256::new();
    for size in (16..=512).step_by(16) {
        for aes in [false, true] {
            let code = payload::fast_aes(size, aes).unwrap();
            assert!(payload::PREFIX_SIZE + code.len() <= 1024);
            hash.update(code);
        }
    }
    assert_eq!(
        format!("{:x}", hash.finalize()),
        fixture["payloads_sha256"].as_str().unwrap()
    );
    for size in [0, 15, 513, usize::MAX] {
        assert!(payload::fast_aes(size, true).is_err());
    }
}

impl Script {
    fn fast(&mut self, input: &[u8], iv: &[u8], aes: bool, output: &[u8]) {
        self.fast_key(input, iv, aes, 1, output);
    }

    fn fast_key(&mut self, input: &[u8], iv: &[u8], aes: bool, key: u32, output: &[u8]) {
        self.clean();
        let mut wire = vec![0; payload::PREFIX_SIZE];
        wire[..4].fill(0xff);
        wire[16..16 + input.len()].copy_from_slice(input);
        wire[16 + input.len()..32 + input.len()].copy_from_slice(iv);
        wire.extend_from_slice(&payload::fast_aes_key(input.len(), aes, key).unwrap());
        self.push((0x21, 1, 0, 0), &wire);
        self.clean();
        self.push((0xa1, 2, 0, 0), &[0; 64]);
        let mut response = vec![0; 16];
        response.extend_from_slice(output);
        self.push((0xa0, 0xfe, 0xeaff, 6), &response);
    }

    fn echo_sweep(&mut self) {
        let sample = (0..512)
            .map(|i| (i * 37 + i / 19) as u8)
            .collect::<Vec<_>>();
        for size in [48, 64, 112, 512] {
            self.fast(&sample[..size], &[0; 16], false, &sample[..size]);
        }
    }

    fn idle_cleanup(&mut self) {
        self.state(9);
        self.command(6);
        self.state(2);
    }
}

#[test]
fn fast_cbc_chains_iv_handles_tail_and_reports_progress() {
    let (cipher, plain) = fast_fixture();
    for offset in [0, 16, 512] {
        for chunk in [16, 48, 496, 512] {
            let size = 528;
            let iv: [u8; 16] = if offset == 0 {
                [0x71; 16]
            } else {
                cipher[offset - 16..offset].try_into().unwrap()
            };
            let mut script = Script::default();
            script.state(2);
            script.echo_sweep();
            for start in (offset..offset + size).step_by(chunk) {
                let end = (start + chunk).min(offset + size);
                let previous = if start == offset {
                    &iv[..]
                } else {
                    &cipher[start - 16..start]
                };
                script.fast(&cipher[start..end], previous, true, &plain[start..end]);
            }
            script.idle_cleanup();
            let mut dfu = Dfu { transport: script };
            let mut progress = Vec::new();
            let output = dfu
                .decrypt_checked(
                    &cipher[offset..offset + size],
                    DecryptOptions {
                        iv,
                        chunk_size: chunk,
                    },
                    Some(&plain[offset..offset + size]),
                    |p| {
                        progress.push(p);
                        Ok(())
                    },
                )
                .unwrap();
            assert_eq!(output.plaintext, plain[offset..offset + size]);
            assert_eq!(output.report.calls, size.div_ceil(chunk));
            assert!(output.report.reference_matched);
            assert_eq!(output.report.cleanup, Cleanup::Idle);
            assert_eq!(progress.last().unwrap().completed, size);
            assert_eq!(progress.last().unwrap().stage, "decrypt");
            assert!(dfu.transport.steps.is_empty());
        }
    }
}

#[test]
fn fast_failures_stop_without_retry_and_always_clean_up() {
    let (cipher, plain) = fast_fixture();
    for fault in [
        "download",
        "prime",
        "reply",
        "timeout",
        "status",
        "unchanged",
        "mismatch",
        "echo",
        "callback",
        "cleanup",
    ] {
        let mut script = Script::default();
        script.state(2);
        script.echo_sweep();
        let start = script.steps.len();
        script.fast(&cipher[..512], &[0x71; 16], true, &plain[..512]);
        match fault {
            "download" => {
                script.steps[start + 2].count -= 1;
                script.steps.truncate(start + 3);
            }
            "prime" => {
                script.steps[start + 5].count -= 1;
                script.steps.truncate(start + 6);
            }
            "reply" => script.steps.back_mut().unwrap().count -= 1,
            "timeout" => script.steps.back_mut().unwrap().count = usize::MAX,
            "status" => script.steps.back_mut().unwrap().data[0] = 1,
            "unchanged" => script.steps.back_mut().unwrap().data.fill(0xa5),
            "mismatch" => script.steps.back_mut().unwrap().data[16] ^= 1,
            "echo" => {
                script.steps[7].data[16] ^= 1;
                script.steps.truncate(8);
            }
            "callback" => script.steps.truncate(8),
            _ => {}
        }
        script.idle_cleanup();
        if fault == "cleanup" {
            script.steps.back_mut().unwrap().data[0] = 10;
        }
        let mut dfu = Dfu { transport: script };
        let error = dfu
            .decrypt_checked(
                &cipher[..512],
                DecryptOptions {
                    iv: [0x71; 16],
                    chunk_size: 512,
                },
                Some(&plain[..512]),
                |_| {
                    if fault == "callback" {
                        Err(invalid("cancelled"))
                    } else {
                        Ok(())
                    }
                },
            )
            .unwrap_err();
        if fault == "cleanup" {
            assert!(error.to_string().contains("cleanup failed"));
        }
        assert!(dfu.transport.steps.is_empty(), "{fault}");
    }
}

#[test]
fn invalid_aes_inputs_never_touch_transport() {
    let mut dfu = Dfu {
        transport: Script::default(),
    };
    for (size, chunk) in [(0, 512), (15, 512), (16, 0), (16, 513), (16, 17)] {
        assert!(dfu
            .decrypt_checked(
                &vec![0; size],
                DecryptOptions {
                    iv: [0; 16],
                    chunk_size: chunk
                },
                None,
                |_| Ok(())
            )
            .is_err());
    }
    assert!(dfu
        .decrypt_checked(&[0; 16], DecryptOptions::default(), Some(&[0; 32]), |_| Ok(
            ()
        ))
        .is_err());
}

#[test]
fn fast_nor_payloads_match_independent_go_assembler() {
    use sha2::{Digest, Sha256};
    let fixture: serde_json::Value =
        serde_json::from_str(include_str!("testdata/fast-aes.json")).unwrap();
    let mut hash = Sha256::new();
    for (offset, size) in [
        (0, 512),
        (512, 509),
        (0xffe00, 512),
        (0xfffff, 1),
        (0, 1),
        (0x100, 256),
    ] {
        let code = payload::fast_nor(offset, size).unwrap();
        assert!(payload::PREFIX_SIZE + code.len() <= 1024);
        hash.update(code);
    }
    assert_eq!(
        format!("{:x}", hash.finalize()),
        fixture["nor_payloads_sha256"].as_str().unwrap()
    );
    for (offset, size) in [
        (0, 0),
        (0, 513),
        (0xfffff, 2),
        (u32::MAX, 1),
        (0, usize::MAX),
    ] {
        assert!(payload::fast_nor(offset, size).is_err());
    }
}

impl Script {
    fn fast_nor(&mut self, bytes: &[u8], offset: usize, size: usize) {
        self.clean();
        let mut wire = vec![0; payload::PREFIX_SIZE];
        wire.extend_from_slice(&payload::fast_nor(offset as u32, size).unwrap());
        self.push((0x21, 1, 0, 0), &wire);
        self.clean();
        self.push((0xa1, 2, 0, 0), &[0; 64]);
        let mut response = vec![0; 16];
        response.extend_from_slice(&bytes[offset..offset + size]);
        self.push((0xa0, 0xfe, 0xeaff, 6), &response);
    }
}

#[test]
fn fast_nor_full_dump_cross_checks_shifted_boundaries_and_edges() {
    let bytes: Vec<u8> = (0..NOR_SIZE)
        .map(|i| (i * 37 + i / 19 + i / 65536) as u8)
        .collect();
    for fault in ["none", "short", "status", "shifted", "edge", "cleanup"] {
        let mut script = Script::default();
        script.state(2);
        script.echo_sweep();
        script.probe(&payload::nor_init(), &[0; 64]);
        let first_reply = script.steps.len() + 6;
        for offset in (0..NOR_SIZE).step_by(512) {
            script.fast_nor(&bytes, offset, 512);
        }
        let shifted_reply = script.steps.len() + 6;
        for offset in (1..NOR_SIZE).step_by(509) {
            script.fast_nor(&bytes, offset, 509.min(NOR_SIZE - offset));
        }
        let edge_reply = script.steps.len() + 6;
        script.nor(&bytes, 0, 64, 60);
        script.nor(&bytes, NOR_SIZE - 64, 64, 60);
        match fault {
            "short" => {
                script.steps[first_reply].count -= 1;
                script.steps.truncate(first_reply + 1);
            }
            "status" => {
                script.steps[first_reply].data[0] = 1;
                script.steps.truncate(first_reply + 1);
            }
            "shifted" => {
                script.steps[shifted_reply].data[16] ^= 1;
                script.steps.truncate(shifted_reply + 1);
            }
            "edge" => {
                script.steps[edge_reply].data[4] ^= 1;
                script.steps.truncate(edge_reply + 8);
            }
            _ => {}
        }
        script.idle_cleanup();
        if fault == "cleanup" {
            script.steps.back_mut().unwrap().data[0] = 10;
        }
        let mut dfu = Dfu { transport: script };
        let mut progress = Vec::new();
        let result = dfu.dump_nor_checked(|p| {
            progress.push(p);
            Ok(())
        });
        if fault == "none" {
            let dump = result.unwrap();
            assert_eq!(dump.bytes, bytes);
            assert!(dump.report.cross_checked);
            assert_eq!(dump.report.cleanup, Cleanup::Idle);
            assert_eq!(progress.last().unwrap().stage, "nor_verify");
            assert_eq!(progress.last().unwrap().completed, NOR_SIZE);
        } else {
            assert!(result.is_err(), "{fault}");
        }
        assert!(dfu.transport.steps.is_empty(), "{fault}");
    }
}

#[test]
fn fast_nor_stops_before_spi_on_failed_echo_or_cancel() {
    for cancel in [false, true] {
        let mut script = Script::default();
        script.state(2);
        script.echo_sweep();
        script.steps.truncate(8);
        if !cancel {
            script.steps.back_mut().unwrap().data[16] ^= 1;
        }
        script.idle_cleanup();
        let mut dfu = Dfu { transport: script };
        assert!(dfu.dump_nor_checked(|_| Err(invalid("cancelled"))).is_err());
        assert!(dfu.transport.steps.is_empty());
    }
}

#[test]
fn cleanup_is_state_specific_and_bounded() {
    for state in 0..=11 {
        let mut script = Script::default();
        script.state(state);
        match state {
            10 => {
                script.command(4);
                script.state(2);
            }
            3 | 5 | 9 => {
                script.command(6);
                script.state(2);
            }
            _ => {}
        }
        let mut dfu = Dfu { transport: script };
        assert_eq!(dfu.idle().is_ok(), matches!(state, 2 | 3 | 5 | 9 | 10));
        assert!(dfu.transport.steps.is_empty());
    }
    let mut script = Script::default();
    script.state(10);
    script.command(4);
    script.state(10);
    assert!(Dfu { transport: script }.idle().is_err());
}

#[test]
fn syscfg_probe_checks_status_short_transfers_and_shifted_reads() {
    for corruption in ["none", "shift", "status", "short", "short_write"] {
        let bytes = fixture();
        let mut script = Script::default();
        script.probe(&payload::nor_init(), &[0; 64]);
        script.nor(&bytes, 0, 24, 24);
        if corruption == "status" {
            script.steps.back_mut().unwrap().data[0] = 1;
        } else if corruption == "short" {
            script.steps.back_mut().unwrap().count = 63;
        } else if corruption == "short_write" {
            // First payload DNLOAD after CLRSTATUS + GETSTATE.
            script.steps[2].count -= 1;
            script.steps.truncate(3);
        } else {
            script.nor(&bytes, 0, bytes.len(), 60);
            script.nor(&bytes, 1, bytes.len() - 1, 59);
            if corruption == "shift" {
                script.steps.back_mut().unwrap().data[4] ^= 1;
            }
        }
        let mut dfu = Dfu { transport: script };
        assert_eq!(dfu.syscfg().is_ok(), corruption == "none");
        assert!(dfu.transport.steps.is_empty(), "{corruption}");
    }
}

fn info() -> DeviceInfo {
    DeviceInfo {
        selector: DeviceSelector { bus: 1, address: 2 },
        port_path: vec![3],
        vendor_id: 0x05ac,
        product_id: 0x1223,
        mode: "test",
        dfu_candidate: true,
    }
}

#[test]
fn unknown_bootrom_never_calls_nor_and_still_cleans_up() {
    for final_state in [2, 10] {
        let mut script = Script::default();
        script.state(2);
        script.rom_preflight(&vec![0; BOOTROM_SIZE]);
        script.state(9);
        script.command(6);
        script.state(final_state);
        let mut dfu = Dfu { transport: script };
        let mut progress = Vec::new();
        let (report, retained_rom) = usb::run_checks(&mut dfu, info(), |event| {
            if let Event::Progress { completed, .. } = event {
                progress.push(completed);
            }
        });
        assert!(retained_rom.is_none());
        assert!(progress.is_empty());
        assert!(!report.compatible);
        assert!(report.identity.is_none());
        assert_eq!(report.checks[3].status, CheckStatus::Failed);
        assert_eq!(report.cleanup == Cleanup::Idle, final_state == 2);
        assert!(dfu.transport.steps.is_empty());
    }
}

#[test]
fn failure_paths_stop_probing_and_preserve_cleanup_result() {
    let mut script = Script::default();
    script.state(4); // dfuDNBUSY
    let mut dfu = Dfu { transport: script };
    let (report, retained_rom) = usb::run_checks(&mut dfu, info(), |_| {});
    assert_eq!(
        retained_rom.is_some(),
        report.compatible && report.cleanup == Cleanup::Idle
    );
    assert!(matches!(report.cleanup, Cleanup::Failed(_)));
    assert_eq!(report.checks[2].status, CheckStatus::Failed);
    assert!(dfu.transport.steps.is_empty());

    let mut script = Script::default();
    script.state(2);
    script.probe(&payload::memory_read(0x20000400), &[0; 64]);
    script.steps.back_mut().unwrap().count = 63;
    script.state(10);
    script.command(4);
    script.state(2);
    let mut dfu = Dfu { transport: script };
    let (report, retained_rom) = usb::run_checks(&mut dfu, info(), |_| {});
    assert_eq!(
        retained_rom.is_some(),
        report.compatible && report.cleanup == Cleanup::Idle
    );
    assert_eq!(report.cleanup, Cleanup::Idle);
    assert!(report.bootrom_sha256.is_none());
    assert!(report.identity.is_none());
    assert!(dfu.transport.steps.is_empty());
}

#[test]
fn checks_recover_previous_transfers_before_probing() {
    for state in [3, 5, 9, 10] {
        let mut script = Script::default();
        script.state(state);
        script.command(if state == 10 { 4 } else { 6 });
        script.state(2);
        script.rom_preflight(&vec![0; BOOTROM_SIZE]);
        script.idle_cleanup();
        let mut dfu = Dfu { transport: script };
        let (report, retained_rom) = usb::run_checks(&mut dfu, info(), |_| {});
        assert_eq!(
            retained_rom.is_some(),
            report.compatible && report.cleanup == Cleanup::Idle
        );
        assert_eq!(report.checks[2].status, CheckStatus::Passed);
        assert_eq!(report.checks[3].status, CheckStatus::Failed);
        assert!(!report.compatible);
        assert_eq!(report.cleanup, Cleanup::Idle);
        assert!(dfu.transport.steps.is_empty());
    }
}

#[test]
fn failed_initial_recovery_never_starts_probes_or_retries() {
    for fault in ["stuck", "short", "timeout"] {
        let mut script = Script::default();
        script.state(10);
        if fault == "stuck" {
            script.command(4);
            script.state(10);
        } else {
            script.steps.back_mut().unwrap().count = if fault == "short" { 0 } else { usize::MAX };
        }
        let mut dfu = Dfu { transport: script };
        let (report, retained_rom) = usb::run_checks(&mut dfu, info(), |_| {});
        assert_eq!(
            retained_rom.is_some(),
            report.compatible && report.cleanup == Cleanup::Idle
        );
        assert!(!report.compatible);
        assert!(matches!(report.cleanup, Cleanup::Failed(_)));
        assert_eq!(report.checks[2].status, CheckStatus::Failed);
        assert_eq!(report.checks[3].status, CheckStatus::Skipped);
        assert!(dfu.transport.steps.is_empty());
    }
}

#[test]
#[ignore = "Requires REPRISE_TEST_BOOTROM and REPRISE_TEST_SYSCFG saved files; never uses USB"]
fn saved_acquisition_replays_complete_check_session() {
    let rom = std::fs::read(std::env::var("REPRISE_TEST_BOOTROM").unwrap()).unwrap();
    let config = std::fs::read(std::env::var("REPRISE_TEST_SYSCFG").unwrap()).unwrap();
    assert_eq!(rom.len(), BOOTROM_SIZE);
    let size = SysCfg::declared_size(&config).unwrap();
    let mut script = Script::default();
    script.state(2);
    script.rom_preflight(&rom);
    script.echo_sweep();
    script.fast_rom(&rom);
    script.probe(&payload::nor_init(), &[0; 64]);
    script.nor(&config, 0, 24, 24);
    script.nor(&config, 0, size, 60);
    script.nor(&config, 1, size - 1, 59);
    script.state(9);
    script.command(6);
    script.state(2);
    let mut dfu = Dfu { transport: script };
    let mut progress = Vec::new();
    let (report, retained_rom) = usb::run_checks(&mut dfu, info(), |event| {
        if let Event::Progress { completed, .. } = event {
            progress.push(completed);
        }
    });
    assert_eq!(progress.last(), Some(&BOOTROM_SIZE));
    assert!(report.compatible, "{report:?}");
    assert_eq!(report.cleanup, Cleanup::Idle);
    assert!(report
        .checks
        .iter()
        .all(|check| check.status == CheckStatus::Passed));
    assert!(dfu.transport.steps.is_empty());
    assert_eq!(retained_rom.as_deref(), Some(rom.as_slice()));
    assert!(check_saved(&rom, &config).compatible);
}

#[test]
fn image_launch_hook_matches_wind3x_assembler() {
    assert_eq!(hex(&payload::image_hook()), "f4e09fe53eff2fe1f0009fe5000090e5ec109fe5010080e0e8109fe50020d1e50030d1e50030c0e5011081e2010080e2012042e2000052e3f8ffff1a150000ea04d04de200e08de5b0409fe5001094e5380791e52c0090e5ac109fe5001081e0a8209fe532ff2fe190009fe5000090e50110a0e32c1080e594109fe5341080e50010a0e3301080e500e09de504d08de20000a0e31eff2fe178009fe578109fe578209fe50030d1e50030c0e5011081e2010080e2012042e2000052e3f8ffff1a38009fe5000090e548109fe5281080e54c009fe54010a0e348209fe532ff2fe144e09fe51eff2fe1140368006100780065006400200064006600750018040020f8ff03227b0100009085022200080000c40a0020312e300000d80322e8840222000100000000002040aa0020d4480020");
}

#[test]
fn helper_launch_polls_download_and_manifest_states() {
    fn script() -> Script {
        let mut s = Script::default();
        s.probe(&payload::image_hook(), &[0; 64]);
        let mut descriptor = vec![20, 3];
        descriptor.extend("haxed dfu".encode_utf16().flat_map(u16::to_le_bytes));
        descriptor.resize(255, 0);
        s.push((0x80, 6, 0x0302, 0), &descriptor);
        s.clean();
        // Inverted IEEE CRC32("123456789") = 0x340bc6d9.
        s.push((0x21, 1, 0, 0), b"123456789\xd9\xc6\x0b\x34");
        s.push((0xa1, 3, 0, 0), &[0, 1, 0, 0, 3, 0]);
        s.push((0xa1, 3, 0, 0), &[0, 1, 0, 0, 4, 0]);
        s.push((0xa1, 3, 0, 0), &[0, 0, 0, 0, 5, 0]);
        s.push((0x21, 1, 1, 0), &[]);
        s.push((0xa1, 3, 0, 0), &[0, 1, 0, 0, 6, 0]);
        s.push((0xa1, 3, 0, 0), &[0, 0, 0, 0, 7, 0]);
        s
    }
    let mut dfu = Dfu {
        transport: script(),
    };
    dfu.launch_image(b"123456789").unwrap();
    assert!(dfu.transport.steps.is_empty());
    for (at, byte) in [(11, 10), (15, 2)] {
        let mut s = script();
        s.steps[at].data[4] = byte;
        assert!(Dfu { transport: s }.launch_image(b"123456789").is_err());
    }
    let mut s = script();
    s.steps.back_mut().unwrap().count = usize::MAX;
    Dfu { transport: s }.launch_image(b"123456789").unwrap();
}

#[test]
fn returned_memory_reads_have_exact_tail_and_cleanup() {
    for size in [64, 96, 320, 512, 513, 2624] {
        let mut s = Script::default();
        for offset in (0..size).step_by(512) {
            let count = 512.min(size - offset);
            s.clean();
            let mut wire = vec![0; payload::PREFIX_SIZE];
            wire.extend(payload::fast_memory(0x2201ef00 + offset as u32, count).unwrap());
            s.push((0x21, 1, 0, 0), &wire);
            s.clean();
            s.push((0xa1, 2, 0, 0), &[0; 64]);
            s.push((0xa0, 0xfe, 0xeaff, 6), &vec![9; count]);
        }
        s.clean();
        let mut dfu = Dfu { transport: s };
        assert_eq!(dfu.read_memory(0x2201ef00, size).unwrap(), vec![9; size]);
        assert!(dfu.transport.steps.is_empty());
    }
}

#[test]
fn ukey_uses_key_selector_two_and_preserves_cbc_tail_and_cleanup() {
    for size in (16..=512).step_by(16) {
        let gkey = payload::fast_aes(size, true).unwrap();
        let ukey = payload::fast_aes_key(size, true, 2).unwrap();
        assert_eq!(gkey.len(), ukey.len());
        let changes: Vec<_> = gkey.iter().zip(&ukey).filter(|(a, b)| a != b).collect();
        assert_eq!(changes, vec![(&1, &2)]);
    }
    let (cipher, plain) = fast_fixture();
    let mut script = Script::default();
    script.state(2);
    script.echo_sweep();
    script.fast_key(&cipher[..512], &[0x71; 16], true, 2, &plain[..512]);
    script.fast_key(
        &cipher[512..528],
        &cipher[496..512],
        true,
        2,
        &plain[512..528],
    );
    script.idle_cleanup();
    let mut dfu = Dfu { transport: script };
    let output = dfu
        .decrypt_key_checked(
            &cipher[..528],
            DecryptOptions {
                iv: [0x71; 16],
                chunk_size: 512,
            },
            Some(&plain[..528]),
            2,
            |_| Ok(()),
        )
        .unwrap();
    assert_eq!(output.plaintext, plain[..528]);
    assert_eq!(output.report.cleanup, Cleanup::Idle);
    assert!(dfu.transport.steps.is_empty());
}

#[test]
fn nor_download_uses_stock_dfu_packets_and_stops_on_rejection() {
    fn script() -> Script {
        let mut script = Script::default();
        script.push((0x21, 1, 0, 0), b"123456789\xd9\xc6\x0b\x34");
        script.push((0xa1, 3, 0, 0), &[0, 0, 0, 0, 5, 0]);
        script.push((0x21, 1, 1, 0), &[]);
        script.push((0xa1, 3, 0, 0), &[0, 1, 0, 0, 6, 0]);
        script.push((0xa1, 3, 0, 0), &[0, 0, 0, 0, 7, 0]);
        script
    }
    let mut dfu = Dfu {
        transport: script(),
    };
    let mut progress = Vec::new();
    dfu.download_image(b"123456789", |done, total| progress.push((done, total)))
        .unwrap();
    assert!(dfu.transport.steps.is_empty());
    assert_eq!(progress, [(13, 13)]);
    for step in [1, 3, 4] {
        let mut transport = script();
        transport.steps[step].data[0] = 7;
        assert!(Dfu { transport }
            .download_image(b"123456789", |_, _| {})
            .is_err());
    }
}
