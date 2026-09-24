// SPDX-License-Identifier: GPL-3.0-only

use crate::{invalid, Result};
use std::collections::BTreeMap;
use uefi_decompress::{decompress_into_with_algo, DecompressionAlgorithm};

const MODULES: &[&str] = &[
    "ROM",
    "ClockAndReset",
    "Cpu",
    "InterruptController",
    "Bds",
    "SoftwareVersion",
];
fn size24(b: &[u8]) -> usize {
    usize::from(b[0]) | usize::from(b[1]) << 8 | usize::from(b[2]) << 16
}

fn word(b: &[u8], p: usize) -> usize {
    u32::from_le_bytes(b[p..p + 4].try_into().unwrap()) as usize
}

pub(super) fn extract(loader: &[u8]) -> Result<BTreeMap<String, Vec<u8>>> {
    let volume = loader
        .get(0x100..)
        .ok_or_else(|| invalid("Missing Apple EFI volume"))?;
    if volume.len() < 0x48
        || &volume[40..44] != b"_FVH"
        || u64::from_le_bytes(volume[32..40].try_into().unwrap()) != volume.len() as u64
    {
        return Err(invalid("Invalid Apple EFI volume header"));
    }
    let mut position = u16::from_le_bytes(volume[48..50].try_into().unwrap()) as usize;
    if position < 0x48 || position > volume.len() || !position.is_multiple_of(8) {
        return Err(invalid("Invalid EFI header length"));
    }
    let mut modules = BTreeMap::new();
    let mut budget = 2 * 1024 * 1024;
    while position < volume.len() {
        let remaining = &volume[position..];
        if remaining.iter().all(|b| *b == 0xff) {
            break;
        }
        let header = remaining
            .get(..24)
            .ok_or_else(|| invalid("Truncated EFI file header"))?;
        let size = size24(&header[20..23]);
        if size < 24 {
            return Err(invalid("Invalid EFI file size"));
        }
        let file = remaining
            .get(24..size)
            .ok_or_else(|| invalid("EFI file outside volume"))?;
        if header[18] != 0xf0 {
            sections(file, 0, &mut budget, &mut modules)?;
        }
        position += (size + 7) & !7;
        if position > volume.len() {
            return Err(invalid("EFI file alignment outside volume"));
        }
    }
    for name in MODULES {
        if !modules.contains_key(&format!("modules/{name}.pe32")) {
            return Err(invalid(format!("Missing Apple EFI module {name}")));
        }
    }
    Ok(modules)
}

fn sections(
    bytes: &[u8],
    depth: usize,
    budget: &mut usize,
    modules: &mut BTreeMap<String, Vec<u8>>,
) -> Result<()> {
    if depth > 4 {
        return Err(invalid("EFI section nesting exceeds limit"));
    }
    let mut position = 0;
    while position < bytes.len() {
        let remaining = &bytes[position..];
        let header = remaining
            .get(..4)
            .ok_or_else(|| invalid("Truncated EFI section"))?;
        let size = size24(header);
        if size < 4 {
            return Err(invalid("Invalid EFI section size"));
        }
        let section = remaining
            .get(4..size)
            .ok_or_else(|| invalid("EFI section outside file"))?;
        match header[3] {
            1 => {
                if section.len() < 13 || section[4] != 1 {
                    return Err(invalid("Unsupported EFI compression"));
                }
                let length = word(section, 0);
                let compressed = &section[5..];
                if length == 0
                    || length > *budget
                    || word(compressed, 4) != length
                    || word(compressed, 0) != compressed.len() - 8
                {
                    return Err(invalid("EFI compression length mismatch"));
                }
                *budget -= length;
                let mut output = vec![0; length];
                decompress_into_with_algo(
                    compressed,
                    &mut output,
                    DecompressionAlgorithm::TianoDecompress,
                )
                .map_err(|e| invalid(format!("EFI decompression: {e:?}")))?;
                sections(&output, depth + 1, budget, modules)?;
            }
            2 => {
                if section.len() < 20 {
                    return Err(invalid("Truncated GUID-defined EFI section"));
                }
                let offset = u16::from_le_bytes(section[16..18].try_into().unwrap()) as usize;
                let attributes = u16::from_le_bytes(section[18..20].try_into().unwrap());
                if offset < 24 || offset > size || attributes & 1 != 0 {
                    return Err(invalid("Unsupported GUID-defined EFI processing"));
                }
                sections(&section[offset - 4..], depth + 1, budget, modules)?;
            }
            16 => {
                for name in MODULES {
                    let marker = format!("{name}.stripped");
                    if section
                        .windows(marker.len())
                        .any(|b| b == marker.as_bytes())
                    {
                        let path = format!("modules/{name}.pe32");
                        if modules.insert(path, section.to_vec()).is_some() {
                            return Err(invalid(format!("Duplicate EFI module {name}")));
                        }
                    }
                }
            }
            18..=21 | 25 => {}
            other => return Err(invalid(format!("Unsupported EFI section type {other}"))),
        }
        let end = position + size;
        position = if end == bytes.len() {
            end
        } else {
            (end + 3) & !3
        };
        if position > bytes.len() {
            return Err(invalid("EFI section alignment outside file"));
        }
    }
    Ok(())
}
