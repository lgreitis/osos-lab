// SPDX-License-Identifier: GPL-3.0-only
//! Shared device identities and verified firmware fingerprints from targets/.

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{collections::BTreeMap, sync::OnceLock};

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Compatibility {
    pub target: String,
    pub models: Vec<String>,
    pub hardware_version: u32,
    /// NOR SysCfg value, separate from the IPSW release version.
    pub apple_firmware: String,
    pub bootrom_sha256: String,
}

impl Compatibility {
    pub fn matches_identity(&self, model: &str, hardware: u32, firmware: &str) -> bool {
        self.models.iter().any(|m| m == model)
            && self.hardware_version == hardware
            && self.apple_firmware == firmware
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
pub struct Target {
    pub target: String,
    pub compatibility: Compatibility,
    pub ipsw: IpswRequirements,
    pub inputs: BTreeMap<String, Fingerprint>,
}

pub fn all() -> &'static [Target] {
    static TARGETS: OnceLock<Vec<Target>> = OnceLock::new();
    TARGETS.get_or_init(|| {
        [
            include_str!("../../../../targets/classic7g-2.0.4.json"),
            include_str!("../../../../targets/classic6g-reva-2.0.1.json"),
        ]
        .into_iter()
        .map(|json| serde_json::from_str(json).expect("valid embedded target profile"))
        .collect()
    })
}

pub fn find(name: &str) -> Option<&'static Target> {
    all().iter().find(|target| target.target == name)
}

pub fn for_identity(model: &str, hardware: u32, firmware: &str) -> Option<&'static Target> {
    all().iter().find(|target| {
        target
            .compatibility
            .matches_identity(model, hardware, firmware)
    })
}

pub fn supports_bootrom(hash: &str) -> bool {
    all()
        .iter()
        .any(|target| target.compatibility.bootrom_sha256 == hash)
}
