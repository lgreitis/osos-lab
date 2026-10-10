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
            let sector_bytes = word(p, 16);
            if matches!(sector_bytes, 512 | 1024 | 2048 | 4096) {
                let scale = u64::from(sector_bytes / 512);
                let start = u64::from(word(p, 4)) * scale;
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
        _ => "unknown validation result",
    };
    log(format!(
        "Layout validation: {reason} (check {error}, partition {})",
        word(raw, 28)
    ));
    Ok(())
}
