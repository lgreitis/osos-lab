// SPDX-License-Identifier: GPL-3.0-only
//! Bounded recipe validation and assembly shared by local builds and bundles.

use super::{pe, INPUT_FILES, INTERFACE};
use crate::{invalid, sha256, valid_hash, Result};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

pub(super) const MAX_RECIPE: usize = 2 * 1024 * 1024;
// OSOS staging space before the companion in classic7g-file-v1.
pub(super) const MAX_OUTPUT: usize = 0x03c00000 - 0x03000000;

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub(super) struct Fingerprint {
    pub(super) bytes: usize,
    pub(super) sha256: String,
}

impl Fingerprint {
    pub(super) fn check(&self, bytes: &[u8]) -> Result<()> {
        if bytes.len() != self.bytes || sha256(bytes) != self.sha256 {
            return Err(invalid("Firmware input/output fingerprint mismatch"));
        }
        Ok(())
    }
}

#[derive(Debug, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
enum Segment {
    Input {
        name: String,
        offset: usize,
        bytes: usize,
    },
    Data {
        offset: usize,
        bytes: usize,
    },
    Zero {
        bytes: usize,
    },
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub(super) struct Output {
    pub(super) bytes: usize,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct Check {
    name: String,
    offset: usize,
    hex: String,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct Relocation {
    base: u32,
    count: usize,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub(super) struct Recipe {
    schema: u32,
    interface: String,
    pub(super) inputs: BTreeMap<String, Fingerprint>,
    pub(super) output: Output,
    data: Fingerprint,
    #[serde(default)]
    checks: Vec<Check>,
    #[serde(default)]
    relocations: BTreeMap<String, Relocation>,
    #[serde(default)]
    ffs_checksums: Vec<usize>,
    segments: Vec<Segment>,
}

pub(super) fn slice(data: &[u8], offset: usize, bytes: usize) -> Result<&[u8]> {
    let end = offset
        .checked_add(bytes)
        .ok_or_else(|| invalid("Recipe range overflow"))?;
    data.get(offset..end)
        .ok_or_else(|| invalid("Recipe range outside input"))
}

impl Recipe {
    pub(super) fn parse(bytes: &[u8]) -> Result<Self> {
        if bytes.len() > MAX_RECIPE {
            return Err(invalid("Assembly recipe exceeds 2 MiB"));
        }
        let recipe: Self = serde_json::from_slice(bytes)?;
        if recipe.schema != 2
            || recipe.interface != INTERFACE
            || recipe.inputs.is_empty()
            || recipe.segments.is_empty()
            || recipe.output.bytes == 0
            || recipe.output.bytes > MAX_OUTPUT
        {
            return Err(invalid("Unsupported assembly recipe"));
        }
        if recipe.data.bytes > MAX_OUTPUT || !valid_hash(&recipe.data.sha256) {
            return Err(invalid("Invalid recipe data fingerprint"));
        }
        for (name, spec) in &recipe.inputs {
            if !INPUT_FILES.iter().any(|(key, _)| key == name)
                || spec.bytes == 0
                || spec.bytes > MAX_OUTPUT
                || !valid_hash(&spec.sha256)
            {
                return Err(invalid("Unsupported Apple input"));
            }
        }
        Ok(recipe)
    }

    pub(super) fn apply(&self, inputs: &BTreeMap<String, Vec<u8>>, data: &[u8]) -> Result<Vec<u8>> {
        for (name, spec) in &self.inputs {
            spec.check(
                inputs
                    .get(name)
                    .ok_or_else(|| invalid(format!("Missing Apple input: {name}")))?,
            )?;
        }
        self.data.check(data)?;
        for check in &self.checks {
            if !self.inputs.contains_key(&check.name)
                || check.hex.is_empty()
                || check.hex.len() % 2 != 0
                || !check.hex.bytes().all(|byte| byte.is_ascii_hexdigit())
            {
                return Err(invalid("Invalid recipe preimage check"));
            }
            let expected: Vec<u8> = check
                .hex
                .as_bytes()
                .chunks_exact(2)
                .map(|pair| u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap())
                .collect();
            if slice(&inputs[&check.name], check.offset, expected.len())? != expected {
                return Err(invalid(format!(
                    "Recipe preimage mismatch: {} at {:#x}",
                    check.name, check.offset
                )));
            }
        }
        let mut relocated = BTreeMap::new();
        for (name, relocation) in &self.relocations {
            if !self.inputs.contains_key(name) {
                return Err(invalid("Relocation uses an unverified input"));
            }
            relocated.insert(
                name,
                pe::relocate(&inputs[name], relocation.base, relocation.count)?,
            );
        }
        let mut output = Vec::with_capacity(self.output.bytes);
        for segment in &self.segments {
            let bytes = match segment {
                Segment::Input { bytes, .. }
                | Segment::Data { bytes, .. }
                | Segment::Zero { bytes } => *bytes,
            };
            if bytes == 0 || bytes > self.output.bytes - output.len() {
                return Err(invalid("Recipe exceeds output bounds"));
            }
            match segment {
                Segment::Input { name, offset, .. } => {
                    if !self.inputs.contains_key(name) {
                        return Err(invalid("Recipe uses an unverified input"));
                    }
                    let source = relocated.get(name).unwrap_or(&inputs[name]);
                    output.extend_from_slice(slice(source, *offset, bytes)?);
                }
                Segment::Data { offset, .. } => {
                    output.extend_from_slice(slice(data, *offset, bytes)?)
                }
                Segment::Zero { .. } => output.resize(output.len() + bytes, 0),
            }
        }
        if output.len() != self.output.bytes {
            return Err(invalid("Recipe output size mismatch"));
        }
        for offset in &self.ffs_checksums {
            update_ffs_checksum(&mut output, *offset)?;
        }
        Ok(output)
    }
}

pub(super) fn update_ffs_checksum(image: &mut [u8], offset: usize) -> Result<()> {
    let header = slice(image, offset, 24)?;
    let size =
        usize::from(header[20]) | usize::from(header[21]) << 8 | usize::from(header[22]) << 16;
    let header_sum = header
        .iter()
        .enumerate()
        .filter(|(i, _)| *i != 17 && *i != 23)
        .fold(0u8, |sum, (_, byte)| sum.wrapping_add(*byte));
    if size < 24 || header[18..20] != [4, 0x40] || header_sum != 0 {
        return Err(invalid("Invalid PreEfi FFS header"));
    }
    let body = &slice(image, offset, size)?[24..];
    image[offset + 17] = body.iter().fold(0u8, |sum, byte| sum.wrapping_sub(*byte));
    Ok(())
}
