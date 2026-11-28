// SPDX-License-Identifier: GPL-3.0-only

use super::protocol::word;
use crate::{exact, invalid, Result};

pub(super) const DIAGNOSTICS: u8 = 0x56;
pub(super) const DIAGNOSTICS_SIZE: usize = 312;

pub(super) fn log_storage(raw: &[u8], log: &mut dyn FnMut(String)) -> Result<()> {
    exact("Storage diagnostics", raw.len(), DIAGNOSTICS_SIZE)?;
    if word(raw, 0) != 0x53544731 || word(raw, 4) != 1 {
        return Err(invalid("Unrecognized storage diagnostics format"));
    }
    let sectors = u64::from(word(raw, 12)) | (u64::from(word(raw, 16)) << 32);
    log(format!(
        "Disk: {sectors} sectors; sector size: {} bytes; battery: {} mV",
        word(raw, 8),
        word(raw, 20)
    ));
    log(format!(
        "Layout reads: mask={:#x}; MBR rc={}; partition rc=[{}, {}, {}, {}]",
        word(raw, 32),
        word(raw, 36) as i32,
        word(raw, 40) as i32,
        word(raw, 44) as i32,
        word(raw, 48) as i32,
        word(raw, 52) as i32
    ));
    for i in 0..4 {
        let at = 56 + i * 64;
        let p = &raw[at..at + 64];
        if word(p, 0) == 0 && word(p, 8) == 0 {
            continue;
        }
        log(format!(
            "Partition {}: type={:#04x}; start={}; length={}; boot-sector read LBA={}",
            i + 1,
            word(p, 0),
            word(p, 4),
            word(p, 8),
            word(p, 12)
        ));
        if matches!(word(p, 0), 0x0b | 0x0c) {
            log(format!("Partition {} FAT32: sector bytes={}; cluster sectors={}; reserved sectors={}; FATs={}; volume sectors={}; FAT sectors={}; root cluster={}; flags={:#x}", i + 1, word(p, 16), word(p, 20), word(p, 24), word(p, 28), word(p, 32), word(p, 36), word(p, 40), word(p, 44)));
            log(format!("Partition {} FAT32: version={}; legacy root entries={}; legacy volume sectors={}; legacy FAT sectors={}", i + 1, word(p, 60), word(p, 48), word(p, 52), word(p, 56)));
            let raw_start = u64::from(word(p, 4));
            let start = u64::from(word(p, 12));
            if raw_start != 0
                && start % raw_start == 0
                && matches!(start / raw_start, 1 | 2 | 4 | 8)
            {
                let scale = start / raw_start;
                let end = start + u64::from(word(p, 8)) * scale;
                log(format!("Partition {} bounds in 512-byte sectors: start={start}; end={end}; disk capacity={sectors}", i + 1));
            }
        }
    }
    let error = word(raw, 24);
    let reason = match error {
        0 => "passed",
        1 => "unsupported physical sector size",
        2 => "failed to read the partition table",
        3 => "invalid MBR signature",
        4 => "no MBR FAT32 partition",
        5 => "multiple FAT32 partitions",
        6 => "failed to read the FAT32 boot sector",
        7 => "invalid FAT32 sector size",
        8 => "FAT32 partition has an invalid start or extends beyond disk capacity",
        9 => "FAT32 boot-sector location does not match the partition start",
        10 => "unsupported or inconsistent FAT32 geometry",
        11 => "invalid FAT32 boot-sector signature",
        12 => "cluster count is outside the FAT32 range",
        13 => "FAT is too small for the volume's cluster count",
        14 => "root cluster is outside the data region",
        15 => "another partition has an invalid start or extends beyond disk capacity",
        16 => "another partition overlaps the FAT32 data partition",
        17 => "cluster size is zero or not a power of two",
        18 => "reserved-sector count is zero",
        19 => "FAT count must be one or two",
        20 => "FAT32 table size is zero",
        21 => "FAT32 volume is larger than its partition-table length",
        22 => "reserved sectors and FAT tables leave no data region",
        23 => "FAT32 legacy root-entry count must be zero",
        24 => "FAT32 legacy 16-bit volume size must be zero",
        25 => "FAT32 legacy 16-bit FAT size must be zero",
        26 => "unsupported FAT32 filesystem version",
        27 => "Apple Partition Map detected; the HFS+ volume is checked instead",
        _ => "unknown validation result",
    };
    log(format!(
        "Layout validation: {reason} (check {error}, partition {})",
        word(raw, 28)
    ));
    Ok(())
}

pub(super) const PROBE: u8 = 0x57;
pub(super) const ATTEMPT: u8 = 0x58;
pub(super) const PROBE_SIZE: usize = 96;
pub(super) const ATTEMPT_SIZE: usize = 1056;

pub(super) fn log_probe(raw: &[u8], log: &mut dyn FnMut(String)) -> Result<u16> {
    exact("Storage probe", raw.len(), PROBE_SIZE)?;
    if word(raw, 0) != 0x53545031 || word(raw, 4) != 1 || word(raw, 8) > 20 {
        return Err(invalid("Unrecognized storage probe format"));
    }
    let text = |bytes: &[u8]| -> String {
        bytes
            .iter()
            .map(|&b| {
                if (32..=126).contains(&b) {
                    char::from(b)
                } else {
                    ' '
                }
            })
            .collect::<String>()
            .trim()
            .into()
    };
    let flags = word(raw, 12);
    let mode = word(raw, 16);
    let transfer = if flags & 1 != 0 {
        "CE-ATA".into()
    } else if flags & 2 == 0 {
        "PIO".into()
    } else if mode & 0xf0 == 0x40 {
        format!("UDMA{}", mode & 0xf)
    } else if mode & 0xf0 == 0x20 {
        format!("MWDMA{}", mode & 0xf)
    } else {
        format!("DMA ({mode:#x})")
    };
    log(format!("ATA: model=\"{}\"; firmware=\"{}\"; transfer={transfer}; addressing=LBA{}; PIO timing={:#x}", text(&raw[32..72]), text(&raw[72..80]), if flags & 4 != 0 { 48 } else { 28 }, word(raw, 20)));
    log(format!("ATA sectors: logical={} bytes; reported physical={} bytes; driver physical multiplier={}; identify[106,117,118]=[{:#06x}, {:#06x}, {:#06x}]", word(raw, 24), word(raw, 28), word(raw, 92), word(raw, 80), word(raw, 84), word(raw, 88)));
    Ok(word(raw, 8) as u16)
}

pub(super) fn log_attempt(
    raw: &[u8],
    index: u16,
    directory: Option<&std::path::Path>,
    log: &mut dyn FnMut(String),
) -> Result<()> {
    exact("Storage read attempt", raw.len(), ATTEMPT_SIZE)?;
    let slot = word(raw, 0);
    let mult = word(raw, 4);
    let lba = u64::from(word(raw, 8)) | (u64::from(word(raw, 28)) << 32);
    let rc = word(raw, 12) as i32;
    let repeat_rc = word(raw, 16) as i32;
    let result = word(raw, 24);
    if slot > 4 || !matches!(mult, 1 | 2 | 4 | 8) || result > 9 {
        return Err(invalid("Invalid storage read attempt"));
    }
    let label = if slot == 0 {
        "MBR".into()
    } else {
        format!("Partition {slot}")
    };
    let reason = match result {
        0 => "header accepted",
        1 => "read failed",
        2 => "repeat read failed",
        3 => "repeated reads differ",
        4 => "invalid boot signature",
        5 => "invalid FAT sector size",
        6 => "skipped: partition exceeds disk or address range",
        7 => "other partition header",
        8 => "inner MBR candidate",
        9 => "inner FAT32 candidate",
        _ => unreachable!(),
    };
    let matches = if rc == 0 && repeat_rc == 0 {
        if word(raw, 20) == 1 {
            "yes"
        } else {
            "no"
        }
    } else {
        "unavailable"
    };
    let header = if rc == 0 {
        let sector = &raw[32..544];
        format!(
            "signature={:02x}{:02x}; FAT sector bytes={}; all zero={}",
            sector[510],
            sector[511],
            u16::from_le_bytes([sector[11], sector[12]]),
            sector.iter().all(|&b| b == 0)
        )
    } else {
        "header unavailable".into()
    };
    log(format!("{label} read: multiplier={mult}; LBA={lba}; rc={rc}; repeat rc={repeat_rc}; match={matches}; {header}; {reason}"));
    if let Some(directory) = directory {
        for (read, code) in [rc, repeat_rc].into_iter().enumerate() {
            if code != 0 {
                continue;
            }
            let name = format!(
                "storage-{index:02}-p{slot}-x{mult}-lba{lba}-read{}.bin",
                read + 1
            );
            std::fs::write(
                directory.join(&name),
                &raw[32 + read * 512..32 + (read + 1) * 512],
            )
            .map_err(|e| invalid(format!("Saving {name}: {e}")))?;
            log(format!("Saved sector: {name}"));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn set(raw: &mut [u8], offset: usize, value: u32) {
        raw[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }

    #[test]
    fn adapter_identity_mode_and_geometry_are_decoded_and_bounded() {
        let mut raw = [0; PROBE_SIZE];
        for (offset, value) in [
            (0, 0x53545031),
            (4, 1),
            (8, 5),
            (12, 6),
            (16, 0x42),
            (20, 0x2072),
            (24, 512),
            (28, 4096),
            (92, 8),
        ] {
            set(&mut raw, offset, value);
        }
        raw[32..39].copy_from_slice(b"FC-1307");
        raw[72..78].copy_from_slice(b"Rev1.4");
        let mut logs = Vec::new();
        assert_eq!(log_probe(&raw, &mut |s| logs.push(s)).unwrap(), 5);
        assert!(logs[0]
            .contains("model=\"FC-1307\"; firmware=\"Rev1.4\"; transfer=UDMA2; addressing=LBA48"));
        assert!(logs[1].contains(
            "logical=512 bytes; reported physical=4096 bytes; driver physical multiplier=8"
        ));
        assert!(log_probe(&raw[..95], &mut |_| {}).is_err());
        set(&mut raw, 8, 21);
        assert!(log_probe(&raw, &mut |_| {}).is_err());
        set(&mut raw, 8, 5);
        set(&mut raw, 4, 2);
        assert!(log_probe(&raw, &mut |_| {}).is_err());
    }

    #[test]
    fn raw_reads_survive_rejected_headers_and_mismatches() {
        let directory =
            std::env::temp_dir().join(format!("reprise-storage-probe-{}", std::process::id()));
        std::fs::create_dir(&directory).unwrap();
        let mut raw = [0; ATTEMPT_SIZE];
        for (offset, value) in [(0, 1), (4, 8), (8, 394224), (20, 1), (24, 4)] {
            set(&mut raw, offset, value);
        }
        let mut logs = Vec::new();
        log_attempt(&raw, 0, Some(&directory), &mut |s| logs.push(s)).unwrap();
        assert!(logs[0].contains(
            "match=yes; signature=0000; FAT sector bytes=0; all zero=true; invalid boot signature"
        ));
        assert_eq!(
            std::fs::read(directory.join("storage-00-p1-x8-lba394224-read1.bin")).unwrap(),
            vec![0; 512]
        );
        assert_eq!(
            std::fs::read(directory.join("storage-00-p1-x8-lba394224-read2.bin")).unwrap(),
            vec![0; 512]
        );
        raw[544] = 0x42;
        set(&mut raw, 20, 0);
        set(&mut raw, 24, 3);
        logs.clear();
        log_attempt(&raw, 1, Some(&directory), &mut |s| logs.push(s)).unwrap();
        assert!(logs[0].contains("match=no"));
        assert_eq!(
            std::fs::read(directory.join("storage-01-p1-x8-lba394224-read2.bin")).unwrap()[0],
            0x42
        );
        set(&mut raw, 12, (-9i32) as u32);
        set(&mut raw, 16, (-200i32) as u32);
        set(&mut raw, 24, 1);
        logs.clear();
        log_attempt(&raw, 2, Some(&directory), &mut |s| logs.push(s)).unwrap();
        assert_eq!(logs.len(), 1);
        assert!(logs[0].contains("match=unavailable; header unavailable; read failed"));
        assert!(!directory
            .join("storage-02-p1-x8-lba394224-read1.bin")
            .exists());
        assert!(log_attempt(&raw[..32], 0, None, &mut |_| {}).is_err());
        std::fs::remove_dir_all(&directory).unwrap();
        set(&mut raw, 12, 0);
        assert!(log_attempt(&raw, 3, Some(&directory), &mut |_| {}).is_err());
    }
}
