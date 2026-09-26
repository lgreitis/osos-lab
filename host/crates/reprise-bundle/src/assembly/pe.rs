// SPDX-License-Identifier: GPL-3.0-only

use super::{invalid, slice, Result};
use std::collections::BTreeSet;

fn half(bytes: &[u8], offset: usize) -> Result<usize> {
    Ok(u16::from_le_bytes(slice(bytes, offset, 2)?.try_into().unwrap()) as usize)
}

fn word(bytes: &[u8], offset: usize) -> Result<usize> {
    Ok(u32::from_le_bytes(slice(bytes, offset, 4)?.try_into().unwrap()) as usize)
}

/// Apple modules use a zero image base and identity-mapped PE32 sections.
pub(super) fn relocate(image: &[u8], base: u32, count: usize) -> Result<Vec<u8>> {
    if slice(image, 0, 2)? != b"MZ"
        || word(image, 0x3c)? != 0x80
        || slice(image, 0x80, 4)? != b"PE\0\0"
        || half(image, 0x98)? != 0x10b
        || word(image, 0xb4)? != 0
        || word(image, 0xd0)? != image.len()
        || u64::from(base) + image.len() as u64 > 1 << 32
        || count == 0
    {
        return Err(invalid("Unsupported Apple PE32 layout"));
    }
    let sections = 0x98 + half(image, 0x94)?;
    for i in 0..half(image, 0x86)? {
        let section = sections + i * 40;
        let size = word(image, section + 8)?;
        let offset = word(image, section + 12)?;
        if offset != word(image, section + 20)? || size != word(image, section + 16)? {
            return Err(invalid("Nonidentity Apple PE32 section"));
        }
        slice(image, offset, size)?;
    }
    let start = word(image, 0x120)?;
    let size = word(image, 0x124)?;
    let relocations = slice(image, start, size)?;
    let mut cursor = 0;
    let mut sites = BTreeSet::new();
    let mut output = image.to_vec();
    while cursor < relocations.len() {
        let page = word(relocations, cursor)?;
        let block_size = word(relocations, cursor + 4)?;
        if block_size < 8 || block_size % 2 != 0 {
            return Err(invalid("Invalid PE32 relocation block"));
        }
        let block = slice(relocations, cursor, block_size)?;
        for offset in (8..block_size).step_by(2) {
            let entry = half(block, offset)?;
            if entry >> 12 == 0 {
                continue;
            }
            let site = page
                .checked_add(entry & 0xfff)
                .ok_or_else(|| invalid("PE32 relocation overflow"))?;
            let value = word(image, site)?;
            if entry >> 12 != 3
                || value >= image.len()
                || sites
                    .range(site.saturating_sub(3)..site + 4)
                    .next()
                    .is_some()
            {
                return Err(invalid("Unsupported or overlapping PE32 relocation"));
            }
            sites.insert(site);
            let value = base
                .checked_add(value as u32)
                .ok_or_else(|| invalid("PE32 relocation overflow"))?;
            output[site..site + 4].copy_from_slice(&value.to_le_bytes());
        }
        cursor += block_size;
    }
    if sites.len() != count {
        return Err(invalid("Apple PE32 relocation count changed"));
    }
    Ok(output)
}
