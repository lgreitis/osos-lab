// SPDX-License-Identifier: GPL-3.0-only

use crate::{invalid, Result};
use serde::Serialize;
use std::collections::BTreeMap;

pub(crate) const MAX_SIZE: usize = 0x2000;
const HEADER_SIZE: usize = 24;

#[derive(Debug, Serialize)]
pub struct SysCfg {
    pub size: usize,
    /// Unknown tags are retained. Every record has exactly 16 data bytes.
    pub entries: BTreeMap<String, [u8; 16]>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct Identity {
    pub model: String,
    pub serial: String,
    pub hardware_id: [u8; 16],
    pub hardware_version: u32,
    pub recorded_firmware: String,
}

fn word(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}

impl SysCfg {
    pub(crate) fn declared_size(bytes: &[u8]) -> Result<usize> {
        if bytes.len() < HEADER_SIZE {
            return Err(invalid("Truncated SysCfg header"));
        }
        if word(bytes, 0) != 0x53436667
            || word(bytes, 8) != 0x2000
            || word(bytes, 12) != 0x10001
            || word(bytes, 16) != 0
        {
            return Err(invalid("Unsupported SysCfg header"));
        }
        let size = word(bytes, 4) as usize;
        let count = word(bytes, 20) as usize;
        if !(HEADER_SIZE..=MAX_SIZE).contains(&size)
            || count > (MAX_SIZE - HEADER_SIZE) / 20
            || size != HEADER_SIZE + count * 20
        {
            return Err(invalid("Invalid SysCfg size or record count"));
        }
        Ok(size)
    }

    pub fn parse(bytes: &[u8]) -> Result<Self> {
        let size = Self::declared_size(bytes)?;
        if bytes.len() < size {
            return Err(invalid("Truncated SysCfg records"));
        }
        let mut entries = BTreeMap::new();
        for record in bytes[HEADER_SIZE..size].chunks_exact(20) {
            let tag: Vec<u8> = record[..4].iter().rev().copied().collect();
            if !tag.iter().all(|b| b.is_ascii_graphic()) {
                return Err(invalid("Invalid SysCfg tag"));
            }
            let tag = String::from_utf8(tag).unwrap();
            if entries
                .insert(tag.clone(), record[4..20].try_into().unwrap())
                .is_some()
            {
                return Err(invalid(format!("Duplicate SysCfg tag {tag}")));
            }
        }
        Ok(Self { size, entries })
    }

    fn entry(&self, tag: &str) -> Result<&[u8; 16]> {
        self.entries
            .get(tag)
            .ok_or_else(|| invalid(format!("Missing SysCfg tag {tag}")))
    }

    fn text(&self, tag: &str) -> Result<String> {
        let raw = self.entry(tag)?;
        let end = raw.iter().position(|byte| *byte == 0).unwrap_or(raw.len());
        if end == 0
            || !raw[..end].iter().all(|b| b.is_ascii_graphic())
            || raw[end..].iter().any(|b| *b != 0)
        {
            return Err(invalid(format!("Invalid SysCfg text in {tag}")));
        }
        Ok(String::from_utf8(raw[..end].to_vec()).unwrap())
    }

    pub fn identity(&self) -> Result<Identity> {
        Ok(Identity {
            model: self.text("Mod#")?,
            serial: self.text("SrNm")?,
            hardware_id: *self.entry("HwId")?,
            hardware_version: word(self.entry("HwVr")?, 4),
            recorded_firmware: self.text("SwVr")?,
        })
    }
}
