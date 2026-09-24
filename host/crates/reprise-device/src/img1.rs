// SPDX-License-Identifier: GPL-3.0-only

use crate::{invalid, Result};
use std::ops::Range;

/// Locate the encrypted body in a Classic IMG1, including its final AES block.
pub fn classic_img1_body_range(bytes: &[u8]) -> Result<Range<usize>> {
    if bytes.len() < 0x800 {
        return Err(invalid("IMG1 header is truncated"));
    }
    if &bytes[..4] != b"8702" || &bytes[4..7] != b"1.0" {
        return Err(invalid("Expected Classic IMG1 (8702 / 1.0)"));
    }
    if !matches!(bytes[7], 1 | 3) {
        return Err(invalid(format!(
            "Expected encrypted IMG1 format 1 or 3, got {}",
            bytes[7]
        )));
    }
    let logical_size = u32::from_le_bytes(bytes[12..16].try_into().unwrap()) as usize;
    if logical_size == 0 {
        return Err(invalid("IMG1 body must be nonempty"));
    }

    // The final partial block needs its original encrypted padding.
    let encrypted_size = logical_size
        .checked_add(15)
        .ok_or_else(|| invalid("IMG1 body length overflow"))?
        & !15;
    let end = encrypted_size
        .checked_add(0x800)
        .ok_or_else(|| invalid("IMG1 body range overflow"))?;
    if end > bytes.len() {
        return Err(invalid("IMG1 encrypted body is truncated"));
    }
    Ok(0x800..end)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn image() -> Vec<u8> {
        let mut bytes = vec![0; 0x800 + 32];
        bytes[..7].copy_from_slice(b"87021.0");
        bytes[7] = 3;
        bytes[12..16].copy_from_slice(&24u32.to_le_bytes());
        bytes
    }

    #[test]
    fn includes_final_ciphertext_block_and_excludes_footer() {
        for format in [1, 3] {
            let mut bytes = image();
            bytes[7] = format;
            bytes.extend_from_slice(&[0xcc; 137]);
            assert_eq!(classic_img1_body_range(&bytes).unwrap(), 0x800..0x820);
        }
    }

    #[test]
    fn accepts_bodies_larger_than_sixteen_mib() {
        let size = 16 * 1024 * 1024 + 16;
        let mut bytes = image();
        bytes.resize(0x800 + size, 0);
        bytes[12..16].copy_from_slice(&(size as u32).to_le_bytes());
        assert_eq!(classic_img1_body_range(&bytes).unwrap(), 0x800..bytes.len());
        crate::DecryptOptions::default()
            .validate(&bytes[0x800..], None)
            .unwrap();
    }

    #[test]
    fn rejects_invalid_headers_lengths_and_missing_padding() {
        let original = image();
        for end in [0, 15, 0x7ff, 0x818, 0x81f] {
            assert!(classic_img1_body_range(&original[..end]).is_err());
        }
        for (offset, value) in [(0, b'9'), (4, b'2'), (7, 0), (7, 2), (7, 4)] {
            let mut bytes = original.clone();
            bytes[offset] = value;
            assert!(classic_img1_body_range(&bytes).is_err());
        }
        for size in [0, 33, u32::MAX] {
            let mut bytes = original.clone();
            bytes[12..16].copy_from_slice(&size.to_le_bytes());
            assert!(classic_img1_body_range(&bytes).is_err());
        }
    }
}
