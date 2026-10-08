// SPDX-License-Identifier: GPL-3.0-only
//! Classic hardware eligibility and the pinned Apple firmware base.

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{collections::BTreeMap, sync::OnceLock};

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Compatibility {
    pub target: String,
    pub hardware_versions: Vec<u32>,
    pub bootrom_sha256: String,
}

impl Compatibility {
    // Model prefixes and installed Apple versions do not select the CFW base.
    pub fn matches_hardware(&self, hardware: u32) -> bool {
        self.hardware_versions.contains(&hardware)
    }
}

#[derive(Clone, Debug, Deserialize)]
pub struct Fingerprint {
    pub bytes: usize,
    pub sha256: String,
}

impl Fingerprint {
    pub fn matches(&self, bytes: &[u8]) -> bool {
        bytes.len() == self.bytes && format!("{:x}", Sha256::digest(bytes)) == self.sha256
    }
}

#[derive(Debug, Deserialize)]
pub struct IpswRequirements {
    pub version: String,
    pub family_id: u32,
    pub updater_family_id: u32,
    pub visible_build_id: u32,
    pub build_id: u32,
    pub encrypted_osos: Fingerprint,
}

#[derive(Debug, Deserialize)]
pub struct Firmware {
    pub target: String,
    pub compatibility: Compatibility,
    pub ipsw: IpswRequirements,
    pub inputs: BTreeMap<String, Fingerprint>,
    pub source: FirmwareSource,
    pub sysinfo_hardware_version: u32,
    pub sysinfo_loader_version: u32,
}

#[derive(Debug, Deserialize)]
pub struct FirmwareSource {
    pub aupd_body: Fingerprint,
    pub loader_offset: usize,
}

pub fn current() -> &'static Firmware {
    static FIRMWARE: OnceLock<Firmware> = OnceLock::new();
    FIRMWARE.get_or_init(|| {
        serde_json::from_str(include_str!("../../../../firmware/apple.json"))
            .expect("valid embedded firmware profile")
    })
}

pub fn for_hardware(hardware: u32) -> Option<&'static Firmware> {
    current()
        .compatibility
        .matches_hardware(hardware)
        .then_some(current())
}

pub fn supports_bootrom(hash: &str) -> bool {
    current().compatibility.bootrom_sha256 == hash
}
