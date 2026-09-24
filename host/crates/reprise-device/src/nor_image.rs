// SPDX-License-Identifier: GPL-3.0-only

use crate::{invalid, Result, SysCfg, NOR_SIZE};
use sha1::Sha1;
use sha2::{Digest, Sha256};

pub const APPLE_LOADER_BYTES: usize = 0x1f800;
pub const APPLE_LOADER_SHA256: &str =
    "7caf3863376cf7890adc73601d24fbf11bc9ce89c7b40366fd61b45a1d6633e8";

/// Apple IM3 in stock or Rockbox dual-boot NOR.
pub struct AppleNorImage<'a> {
    pub offset: usize,
    header: &'a [u8],
    body: &'a [u8],
}

fn word(bytes: &[u8], offset: usize) -> usize {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap()) as usize
}

impl<'a> AppleNorImage<'a> {
    fn at(nor: &'a [u8], offset: usize) -> Result<Self> {
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
        })
    }

    pub fn locate(nor: &'a [u8]) -> Result<Self> {
        if nor.len() != NOR_SIZE {
            return Err(invalid("Expected a full 1 MiB NOR backup"));
        }
        let identity = SysCfg::parse(nor)?.identity()?;
        if !matches!(identity.model.as_str(), "MC293" | "MC297")
            || identity.hardware_version != 0x00130200
            || identity.recorded_firmware != "2.0.4"
        {
            return Err(invalid(
                "NOR SysCfg is incompatible with Classic Rev B / 2.0.4",
            ));
        }
        let first = Self::at(nor, 0x8000)?;
        if first.encrypted() {
            if first.body.len() != APPLE_LOADER_BYTES {
                return Err(invalid("Unsupported encrypted primary NOR loader"));
            }
            return Ok(first);
        }
        if Self::validate_plaintext(first.body).is_ok() {
            return Ok(first);
        }
        let next = 0x8000 + ((0x800 + first.body.len() + 0xfff) & !0xfff);
        let apple = Self::at(nor, next)?;
        if apple.encrypted() {
            return Err(invalid(
                "Expected a decrypted Apple loader after the Rockbox loader",
            ));
        }
        Self::validate_plaintext(apple.body)?;
        Ok(apple)
    }

    pub fn encrypted(&self) -> bool {
        self.header[7] == 1
    }

    pub fn validate_plaintext(plain: &[u8]) -> Result<()> {
        if plain.len() != APPLE_LOADER_BYTES
            || format!("{:x}", Sha256::digest(plain)) != APPLE_LOADER_SHA256
        {
            return Err(invalid(
                "Apple loader does not match the supported 2.0.4 input",
            ));
        }
        Ok(())
    }

    pub fn plaintext(&self) -> Result<&'a [u8]> {
        if self.encrypted() {
            return Err(invalid("Apple NOR loader requires device UKEY decryption"));
        }
        Self::validate_plaintext(self.body)?;
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
        Self::validate_plaintext(&plain)?;
        Ok(plain)
    }
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

    #[test]
    #[ignore = "requires preserved stock NOR and decrypted Apple loader"]
    fn saved_nor_replays_header_body_signatures_and_rejects_corruption() {
        let root = std::path::PathBuf::from(
            std::env::var_os("REPRISE_ASSEMBLY_ROOT").expect("REPRISE_ASSEMBLY_ROOT"),
        );
        let nor = std::fs::read(root.join("inputs/nor.bin")).unwrap();
        let loader = std::fs::read(root.join("inputs/apple-loader.bin")).unwrap();
        let image = AppleNorImage::locate(&nor).unwrap();
        let decrypt = |bytes: &[u8]| -> Result<Vec<u8>> {
            if bytes == &nor[0x8040..0x8050] {
                Ok(Sha1::digest(&nor[0x8000..0x8040])[..16].to_vec())
            } else if bytes == &nor[0x8010..0x8020] {
                Ok(Sha1::digest(&loader)[..16].to_vec())
            } else if bytes == &nor[0x8800..0x8800 + APPLE_LOADER_BYTES] {
                Ok(loader.clone())
            } else {
                Err(invalid("Unexpected ciphertext"))
            }
        };
        assert_eq!(image.decrypt(decrypt).unwrap(), loader);
        for offset in [0x8010, 0x8040, 0x9000] {
            let mut bad = nor.clone();
            bad[offset] ^= 1;
            assert!(AppleNorImage::locate(&bad)
                .unwrap()
                .decrypt(decrypt)
                .is_err());
        }
    }
}
