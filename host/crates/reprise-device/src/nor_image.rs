// SPDX-License-Identifier: GPL-3.0-only

use crate::{
    invalid,
    targets::{self, Fingerprint},
    Result, SysCfg, NOR_SIZE,
};
use sha1::Sha1;
use sha2::Digest;

pub const APPLE_LOADER_BYTES: usize = 0x1f800;
/// Apple IM3 in stock or Rockbox dual-boot NOR.
pub struct AppleNorImage<'a> {
    pub offset: usize,
    header: &'a [u8],
    body: &'a [u8],
    fingerprint: &'a Fingerprint,
}

fn word(bytes: &[u8], offset: usize) -> usize {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap()) as usize
}

impl<'a> AppleNorImage<'a> {
    fn at(nor: &'a [u8], offset: usize, fingerprint: &'a Fingerprint) -> Result<Self> {
        let header = nor
            .get(offset..offset + 0x800)
            .ok_or_else(|| invalid("Truncated NOR IM3 header"))?;
        if &header[..7] != b"87021.0" || !matches!(header[7], 1 | 2) {
            return Err(invalid(format!("Unsupported NOR IM3 at {offset:#x}")));
        }
        let length = word(header, 12);
        if length == 0
            || length > APPLE_LOADER_BYTES
            || !length.is_multiple_of(16)
            || word(header, 8) & !1 >= length
        {
            return Err(invalid("Invalid NOR IM3 size or entrypoint"));
        }
        let body = nor
            .get(offset + 0x800..offset + 0x800 + length)
            .ok_or_else(|| invalid("Truncated NOR IM3 body"))?;
        Ok(Self {
            offset,
            header,
            body,
            fingerprint,
        })
    }

    pub fn locate(nor: &'a [u8]) -> Result<Self> {
        if nor.len() != NOR_SIZE {
            return Err(invalid("Expected a full 1 MiB NOR backup"));
        }
        let identity = SysCfg::parse(nor)?.identity()?;
        let target = targets::for_identity(
            &identity.model,
            identity.hardware_version,
            &identity.recorded_firmware,
        )
        .ok_or_else(|| invalid("NOR SysCfg is not a supported Classic target"))?;
        let fingerprint = &target.inputs["apple-loader.bin"];
        let first = Self::at(nor, 0x8000, fingerprint)?;
        if first.encrypted() {
            if first.body.len() != fingerprint.bytes {
                return Err(invalid("Unsupported encrypted primary NOR loader"));
            }
            return Ok(first);
        }
        if first.validate_plaintext(first.body).is_ok() {
            return Ok(first);
        }
        let next = 0x8000 + ((0x800 + first.body.len() + 0xfff) & !0xfff);
        let apple = Self::at(nor, next, fingerprint)?;
        if apple.encrypted() {
            return Err(invalid(
                "Expected a decrypted Apple loader after the Rockbox loader",
            ));
        }
        apple.validate_plaintext(apple.body)?;
        Ok(apple)
    }

    pub fn encrypted(&self) -> bool {
        self.header[7] == 1
    }

    pub fn validate_plaintext(&self, plain: &[u8]) -> Result<()> {
        if plain.len() != self.body.len() {
            return Err(invalid("Apple loader plaintext length mismatch"));
        }
        if !self.fingerprint.matches(plain) {
            return Err(invalid(
                "Apple loader does not match its target fingerprint",
            ));
        }
        validate_volume(plain)
    }

    pub fn plaintext(&self) -> Result<&'a [u8]> {
        if self.encrypted() {
            return Err(invalid("Apple NOR loader requires device UKEY decryption"));
        }
        self.validate_plaintext(self.body)?;
        Ok(self.body)
    }

    /// Decrypt and verify IM3 using a zero-IV UKEY CBC callback.
    pub fn decrypt(&self, mut decrypt: impl FnMut(&[u8]) -> Result<Vec<u8>>) -> Result<Vec<u8>> {
        if !self.encrypted() {
            return Ok(self.plaintext()?.to_vec());
        }
        let header_hash = decrypt(&self.header[64..80])?;
        if header_hash != Sha1::digest(&self.header[..64])[..16] {
            return Err(invalid("NOR IM3 header signature mismatch"));
        }
        let plain = decrypt(self.body)?;
        if plain.len() != self.body.len() {
            return Err(invalid("Short NOR AES response"));
        }
        let body_hash = decrypt(&self.header[16..32])?;
        if body_hash != Sha1::digest(&plain)[..16] {
            return Err(invalid("NOR IM3 body signature mismatch"));
        }
        self.validate_plaintext(&plain)?;
        Ok(plain)
    }
}

// Check the bounded reset branch and the EFI volume header checksum.
fn validate_volume(plain: &[u8]) -> Result<()> {
    let volume = plain
        .get(0x100..)
        .ok_or_else(|| invalid("Missing Apple EFI volume"))?;
    if volume.len() < 0x48
        || &volume[40..44] != b"_FVH"
        || u64::from_le_bytes(volume[32..40].try_into().unwrap()) != volume.len() as u64
        || word(plain, 0) & 0xff000000 != 0xea000000
    {
        return Err(invalid("Invalid Apple loader vectors or EFI volume"));
    }
    let branch = ((word(plain, 0) as i32) << 8 >> 6) + 8;
    let header_size = u16::from_le_bytes(volume[48..50].try_into().unwrap()) as usize;
    if branch < 0x20
        || branch as usize >= plain.len()
        || header_size < 0x48
        || header_size > volume.len()
        || !header_size.is_multiple_of(8)
    {
        return Err(invalid(
            "Invalid Apple loader entrypoint or EFI header size",
        ));
    }
    let checksum = volume[..header_size]
        .chunks_exact(2)
        .fold(0u16, |sum, pair| {
            sum.wrapping_add(u16::from_le_bytes(pair.try_into().unwrap()))
        });
    if checksum != 0 {
        return Err(invalid("Apple EFI volume header checksum mismatch"));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn nor() -> Vec<u8> {
        let mut nor = crate::tests::fixture();
        nor.resize(NOR_SIZE, 0xff);
        nor[0x8000..0x8800].fill(0);
        nor[0x8000..0x8008].copy_from_slice(b"87021.0\x01");
        nor[0x800c..0x8010].copy_from_slice(&(APPLE_LOADER_BYTES as u32).to_le_bytes());
        nor
    }

    #[test]
    fn nor_layout_rejects_bad_headers_sizes_and_missing_apple_loader() {
        let original = nor();
        assert_eq!(AppleNorImage::locate(&original).unwrap().offset, 0x8000);
        for n in [0, 24, NOR_SIZE - 1] {
            assert!(AppleNorImage::locate(&original[..n]).is_err());
        }
        for (offset, value) in [
            (0x8000, 0),
            (0x8007, 3),
            (0x800a, 0xff),
            (0x800c, 1),
            (0x800f, 0xff),
        ] {
            let mut bad = original.clone();
            bad[offset] = value;
            assert!(AppleNorImage::locate(&bad).is_err(), "{offset:x}");
        }
        let mut modified = original.clone();
        modified[0x8007] = 2;
        assert!(AppleNorImage::locate(&modified).is_err());
    }

    #[test]
    fn im3_integrity_and_plaintext_fingerprint_are_both_required() {
        let mut nor = nor();
        let mut plain = vec![0; 0x300];
        plain[..4].copy_from_slice(&0xea000006u32.to_le_bytes());
        let volume = &mut plain[0x100..];
        volume[32..40].copy_from_slice(&0x200u64.to_le_bytes());
        volume[40..44].copy_from_slice(b"_FVH");
        volume[48..50].copy_from_slice(&0x48u16.to_le_bytes());
        let sum = volume[..0x48].chunks_exact(2).fold(0u16, |s, p| {
            s.wrapping_add(u16::from_le_bytes(p.try_into().unwrap()))
        });
        volume[50..52].copy_from_slice(&0u16.wrapping_sub(sum).to_le_bytes());
        nor[0x800c..0x8010].copy_from_slice(&(plain.len() as u32).to_le_bytes());
        nor[0x8010..0x8020].copy_from_slice(&Sha1::digest(&plain)[..16]);
        let header_hash = Sha1::digest(&nor[0x8000..0x8040]);
        nor[0x8040..0x8050].copy_from_slice(&header_hash[..16]);
        nor[0x8800..0x8800 + plain.len()].copy_from_slice(&plain);
        let fingerprint = Fingerprint {
            bytes: plain.len(),
            sha256: format!("{:x}", sha2::Sha256::digest(&plain)),
        };
        let image = AppleNorImage::at(&nor, 0x8000, &fingerprint).unwrap();
        // Identity callback exercises signature comparisons without hardware AES.
        assert_eq!(image.decrypt(|b| Ok(b.to_vec())).unwrap(), plain);
        let mut corrupt = plain.clone();
        corrupt[0x128] ^= 1;
        assert!(image.validate_plaintext(&corrupt).is_err());
        nor[0x8900] ^= 1;
        assert!(AppleNorImage::at(&nor, 0x8000, &fingerprint)
            .unwrap()
            .decrypt(|b| Ok(b.to_vec()))
            .is_err());
    }

    #[test]
    fn im3_signature_errors_stop_before_publishing_plaintext() {
        let nor = nor();
        let image = AppleNorImage::locate(&nor).unwrap();
        let header_hash = Sha1::digest(&nor[0x8000..0x8040])[..16].to_vec();
        let mut calls = 0;
        assert!(image
            .decrypt(|_| {
                calls += 1;
                Ok(vec![0; 16])
            })
            .is_err());
        assert_eq!(calls, 1);
        for short in [true, false] {
            let mut calls = 0;
            let result = image.decrypt(|_| {
                calls += 1;
                Ok(match calls {
                    1 => header_hash.clone(),
                    2 => vec![0; if short { 16 } else { APPLE_LOADER_BYTES }],
                    _ => vec![0; 16],
                })
            });
            assert!(result.is_err());
            assert_eq!(calls, if short { 2 } else { 3 });
        }
        assert!(image.decrypt(|_| Err(invalid("cancelled"))).is_err());
    }
}
