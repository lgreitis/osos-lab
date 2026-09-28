// SPDX-License-Identifier: GPL-3.0-only

use crate::{invalid, Result};

pub(super) const HEADER_SIZE: usize = 8;

pub(super) fn packet(ty: u8, request: u8, value: u16, index: u16, size: usize) -> Result<Vec<u8>> {
    let length =
        u16::try_from(size).map_err(|_| invalid("Apple DFU control transfer is too large"))?;
    let mut packet = vec![0; HEADER_SIZE + size];
    packet[..2].copy_from_slice(&[ty, request]);
    packet[2..4].copy_from_slice(&value.to_le_bytes());
    packet[4..6].copy_from_slice(&index.to_le_bytes());
    packet[6..8].copy_from_slice(&length.to_le_bytes());
    Ok(packet)
}

pub(super) fn payload_length(transferred: usize, capacity: usize) -> Result<usize> {
    if !(HEADER_SIZE..=HEADER_SIZE + capacity).contains(&transferred) {
        return Err(invalid(format!(
            "Invalid Apple DFU transfer length {transferred}"
        )));
    }
    Ok(transferred - HEADER_SIZE)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encodes_exploit_setup_without_changing_index_or_value() {
        let wire = packet(0xa0, 0xfe, 0xeaff, 6, 528).unwrap();
        assert_eq!(&wire[..8], &[0xa0, 0xfe, 0xff, 0xea, 6, 0, 0x10, 2]);
        assert_eq!(wire.len(), 536);
        assert!(packet(0x21, 1, 0, 0, 65536).is_err());
    }

    #[test]
    fn preserves_short_reads_and_zero_length_requests() {
        assert_eq!(payload_length(9, 6).unwrap(), 1);
        assert_eq!(payload_length(8, 0).unwrap(), 0);
        assert!(payload_length(7, 6).is_err());
        assert!(payload_length(15, 6).is_err());
        assert_eq!(
            packet(0x21, 6, 0, 0, 0).unwrap(),
            [0x21, 6, 0, 0, 0, 0, 0, 0]
        );
    }
}
