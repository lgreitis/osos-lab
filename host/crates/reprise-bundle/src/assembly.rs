// SPDX-License-Identifier: GPL-3.0-only
//! Assemble disk firmware and the dual-boot installer from local Apple inputs.

use crate::{invalid, read_file, sha256, valid_hash, write_directory, Result, VerifiedBundle};
use reprise_device::SysCfg;
use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, path::Path};

pub const INTERFACE: &str = "classic7g-file-v1";
const SYSINFO_OFFSET: usize = 0x25100;
const SYSINFO_BYTES: usize = 0x120;
const MAX_RECIPE: usize = 2 * 1024 * 1024;
// OSOS staging space before the companion in classic7g-file-v1.
const MAX_OUTPUT: usize = 0x03c00000 - 0x03000000;
pub(crate) const INPUT_FILES: &[(&str, &str)] = &[
    ("osos", "osos.bin"),
    ("apple_loader", "apple-loader.bin"),
    ("rom", "modules/ROM.pe32"),
    ("clockandreset", "modules/ClockAndReset.pe32"),
    ("cpu", "modules/Cpu.pe32"),
    ("interruptcontroller", "modules/InterruptController.pe32"),
    ("bds", "modules/Bds.pe32"),
    ("softwareversion", "modules/SoftwareVersion.pe32"),
];

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct Fingerprint {
    bytes: usize,
    sha256: String,
}

impl Fingerprint {
    fn check(&self, bytes: &[u8]) -> Result<()> {
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
struct Recipe {
    schema: u32,
    interface: String,
    inputs: BTreeMap<String, Fingerprint>,
    output: Fingerprint,
    segments: Vec<Segment>,
}

fn slice(data: &[u8], offset: usize, bytes: usize) -> Result<&[u8]> {
    let end = offset
        .checked_add(bytes)
        .ok_or_else(|| invalid("Recipe range overflow"))?;
    data.get(offset..end)
        .ok_or_else(|| invalid("Recipe range outside input"))
}

impl Recipe {
    fn parse(bytes: &[u8]) -> Result<Self> {
        if bytes.len() > MAX_RECIPE {
            return Err(invalid("Assembly recipe exceeds 2 MiB"));
        }
        let recipe: Self = serde_json::from_slice(bytes)?;
        if recipe.schema != 1
            || recipe.interface != INTERFACE
            || recipe.inputs.is_empty()
            || recipe.segments.is_empty()
            || recipe.output.bytes == 0
            || recipe.output.bytes > MAX_OUTPUT
            || !valid_hash(&recipe.output.sha256)
        {
            return Err(invalid("Unsupported assembly recipe"));
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

    fn apply(&self, inputs: &BTreeMap<String, Vec<u8>>, data: &[u8]) -> Result<Vec<u8>> {
        for (name, spec) in &self.inputs {
            spec.check(
                inputs
                    .get(name)
                    .ok_or_else(|| invalid(format!("Missing Apple input: {name}")))?,
            )?;
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
                    output.extend_from_slice(slice(&inputs[name], *offset, bytes)?);
                }
                Segment::Data { offset, .. } => {
                    output.extend_from_slice(slice(data, *offset, bytes)?)
                }
                Segment::Zero { .. } => output.resize(output.len() + bytes, 0),
            }
        }
        self.output.check(&output)?;
        Ok(output)
    }
}

/// Local, decrypted Apple images used by the assembly recipes.
#[derive(Default)]
pub struct AppleInputs(BTreeMap<String, Vec<u8>>);

impl AppleInputs {
    pub fn load(directory: &Path) -> Result<Self> {
        let mut inputs = BTreeMap::new();
        for (name, file) in INPUT_FILES {
            inputs.insert(
                (*name).to_owned(),
                read_file(&directory.join(file), MAX_OUTPUT)?,
            );
        }
        Ok(Self(inputs))
    }

    pub fn insert(&mut self, name: &str, bytes: Vec<u8>) -> Result<()> {
        if !INPUT_FILES.iter().any(|(key, _)| *key == name) || bytes.len() > MAX_OUTPUT {
            return Err(invalid("Unsupported Apple input"));
        }
        self.0.insert(name.to_owned(), bytes);
        Ok(())
    }
}

fn assemble_component(
    bundle: &VerifiedBundle,
    name: &str,
    inputs: &AppleInputs,
) -> Result<Vec<u8>> {
    let recipe = Recipe::parse(bundle.file(name, "recipe")?)?;
    recipe.apply(&inputs.0, bundle.file(name, "data")?)
}

pub fn disk_bytes(bundle: &VerifiedBundle) -> Result<u32> {
    let osos = Recipe::parse(bundle.file("osos", "recipe")?)?;
    let companion = Recipe::parse(bundle.file("companion", "recipe")?)?;
    Ok((osos.output.bytes + companion.output.bytes) as u32)
}

pub fn assemble_osos(bundle: &VerifiedBundle, inputs: &AppleInputs) -> Result<Vec<u8>> {
    let image = assemble_component(bundle, "osos", inputs)?;
    if image.len() <= 0x800 || image.get(..7) != Some(b"87021.0") {
        return Err(invalid("Unsupported assembled OSOS layout"));
    }
    for offset in [12, 16, 20] {
        if word(&image, offset) as usize != image.len() - 0x800 {
            return Err(invalid("Assembled OSOS header length mismatch"));
        }
    }
    Ok(image)
}

pub fn assemble_companion(
    bundle: &VerifiedBundle,
    inputs: &AppleInputs,
    syscfg: &SysCfg,
) -> Result<Vec<u8>> {
    let identity = syscfg.identity().map_err(|e| invalid(e.to_string()))?;
    let target = &bundle.manifest().compatibility;
    if !target.models.contains(&identity.model)
        || target.hardware_version != identity.hardware_version
        || target.apple_firmware != identity.recorded_firmware
    {
        return Err(invalid("SysCfg is incompatible with this companion"));
    }
    let mut image = assemble_component(bundle, "companion", inputs)?;
    if image.len() != 0x2b800
        || image[SYSINFO_OFFSET..SYSINFO_OFFSET + SYSINFO_BYTES]
            .iter()
            .any(|b| *b != 0)
    {
        return Err(invalid(
            "Unsupported companion layout or occupied SysInfo slot",
        ));
    }
    image[SYSINFO_OFFSET..SYSINFO_OFFSET + SYSINFO_BYTES].copy_from_slice(&sysinfo(syscfg)?);
    Ok(image)
}

fn word(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}

fn sysinfo(cfg: &SysCfg) -> Result<[u8; SYSINFO_BYTES]> {
    let entry = |name: &str| {
        cfg.entries
            .get(name)
            .ok_or_else(|| invalid(format!("Missing SysCfg tag: {name}")))
    };
    let mut result = [0; SYSINFO_BYTES];
    for (offset, value) in [
        (0, 0x53797349u32),
        (4, 4),
        (0xe0, 0x04000000),
        (0xe4, 0x08000000),
        (0xe8, 0x40000),
        (0xec, 0x22000000),
        (0xf0, 0x100000),
        (0xf4, 0x24000000),
        (0x118, 0x7672736e),
        (0x11c, 0x01708004),
    ] {
        result[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    result[0x88..0x8b].copy_from_slice(b"NA\0");
    for (tag, start, length, dest) in [
        ("SrNm", 0, 16, 0x18),
        ("FwId", 4, 8, 0x38),
        ("HwVr", 4, 4, 0x84),
        ("Codc", 0, 4, 0x104),
        ("SwVr", 0, 16, 0x108),
        ("Mod#", 0, 16, 0x98),
    ] {
        result[dest..dest + length].copy_from_slice(&entry(tag)?[start..start + length]);
    }
    let region = entry("Regn")?;
    if u16::from_le_bytes(region[..2].try_into().unwrap()) == 1 {
        result[0x92..0x96].copy_from_slice(&region[4..8]);
    }
    Ok(result)
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct NorTemplate {
    schema: u32,
    interface: String,
    bytes: usize,
    sha256: String,
    bootloader_offset: usize,
}

/// Return the compiled Rockbox dual-boot installer.
pub fn nor_installer(bundle: &VerifiedBundle) -> Result<Vec<u8>> {
    let raw = bundle.file("nor", "descriptor")?;
    if raw.len() > 4096 {
        return Err(invalid("NOR descriptor too large"));
    }
    let spec: NorTemplate = serde_json::from_slice(raw)?;
    let image = bundle.file("nor", "image")?;
    if spec.schema != 1
        || spec.interface != INTERFACE
        || image.len() > 0x20000
        || image.len() < 0x800
        || image.get(..8) != Some(b"87021.0\x03")
    {
        return Err(invalid("Unsupported dual-boot installer template"));
    }
    let header_offset = spec
        .bootloader_offset
        .checked_sub(0x800)
        .filter(|offset| *offset >= 0x310)
        .ok_or_else(|| invalid("Invalid bootloader offset"))?;
    let header = slice(image, header_offset, 0x800)?;
    let bootloader = slice(image, spec.bootloader_offset, word(header, 12) as usize)?;
    if header.get(..8) != Some(b"87021.0\x02")
        || header[0x40..0x50].iter().any(|byte| *byte != 0)
        || bootloader.is_empty()
        || bootloader.len() % 16 != 0
        || spec.bootloader_offset + bootloader.len() != image.len()
    {
        return Err(invalid("Invalid dual-boot installer packaging"));
    }
    Fingerprint {
        bytes: spec.bytes,
        sha256: spec.sha256,
    }
    .check(image)?;
    Ok(image.to_vec())
}

pub struct Artifacts {
    pub osos: Vec<u8>,
    pub companion: Vec<u8>,
    pub nor_installer: Vec<u8>,
}

#[derive(Serialize)]
pub struct AssemblyReport {
    pub interface: &'static str,
    pub files: BTreeMap<String, FileReport>,
}

#[derive(Serialize)]
pub struct FileReport {
    pub bytes: usize,
    pub sha256: String,
}

impl Artifacts {
    fn files(&self) -> [(&str, &[u8]); 3] {
        [
            ("osos-cfw.bin", &self.osos),
            ("cfw-loader.bin", &self.companion),
            ("install-rockbox-cfw.dfu", &self.nor_installer),
        ]
    }

    pub fn report(&self) -> AssemblyReport {
        AssemblyReport {
            interface: INTERFACE,
            files: self
                .files()
                .into_iter()
                .map(|(name, bytes)| {
                    (
                        name.to_owned(),
                        FileReport {
                            bytes: bytes.len(),
                            sha256: sha256(bytes),
                        },
                    )
                })
                .collect(),
        }
    }

    /// Write assembled artifacts into a new directory.
    pub fn write(&self, directory: &Path) -> Result<AssemblyReport> {
        let report = self.report();
        let json = serde_json::to_vec_pretty(&report)?;
        write_directory(
            directory,
            self.files()
                .into_iter()
                .chain([("assembly.json", json.as_slice())]),
        )?;
        Ok(report)
    }
}

pub fn assemble(
    bundle: &VerifiedBundle,
    inputs: &AppleInputs,
    nor_backup: &[u8],
) -> Result<Artifacts> {
    if nor_backup.len() != 0x100000 {
        return Err(invalid("Expected a full 1 MiB NOR backup"));
    }
    let syscfg = SysCfg::parse(nor_backup).map_err(|e| invalid(e.to_string()))?;
    Ok(Artifacts {
        osos: assemble_osos(bundle, inputs)?,
        companion: assemble_companion(bundle, inputs, &syscfg)?,
        nor_installer: nor_installer(bundle)?,
    })
}

#[cfg(test)]
mod tests;
