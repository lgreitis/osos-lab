// SPDX-License-Identifier: GPL-3.0-only

use crate::{
    dfu::{Dfu, Transport},
    invalid, CheckId, CheckReport, CheckStatus, Cleanup, Error, Event, Result, BOOTROM_SIZE,
};
use rusb::{Context, Device, DeviceHandle, UsbContext};
use serde::{Deserialize, Serialize};
use std::time::Duration;

const APPLE: u16 = 0x05ac;
const CLASSIC_DFU: u16 = 0x1223;
const TIMEOUT: Duration = Duration::from_millis(50);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct DeviceSelector {
    pub bus: u8,
    pub address: u8,
}

#[derive(Clone, Debug, Serialize)]
pub struct DeviceInfo {
    pub selector: DeviceSelector,
    pub port_path: Vec<u8>,
    pub vendor_id: u16,
    pub product_id: u16,
    pub mode: &'static str,
    pub dfu_candidate: bool,
}

pub(crate) fn usb_error(operation: &'static str, source: rusb::Error) -> Error {
    Error::Usb { operation, source }
}

fn candidates(context: &Context) -> Result<Vec<(Device<Context>, DeviceInfo)>> {
    let devices = context.devices().map_err(|e| usb_error("enumeration", e))?;
    let mut found = Vec::new();
    for device in devices.iter() {
        let desc = device
            .device_descriptor()
            .map_err(|e| usb_error("device descriptor", e))?;
        if desc.vendor_id() != APPLE {
            continue;
        }
        let mode = match desc.product_id() {
            CLASSIC_DFU => "BootROM DFU (Classic/Nano 3G shared ID)",
            0x1241 | 0x1245 | 0x1247 | 0x1250 => "Classic WTF recovery (not BootROM DFU)",
            0x1242 => "Nano 3G WTF recovery (unsupported)",
            0x1261 => "Classic disk/normal mode (not DFU)",
            _ => continue,
        };
        let info = DeviceInfo {
            selector: DeviceSelector {
                bus: device.bus_number(),
                address: device.address(),
            },
            port_path: device
                .port_numbers()
                .map_err(|e| usb_error("port path", e))?,
            vendor_id: desc.vendor_id(),
            product_id: desc.product_id(),
            mode,
            dfu_candidate: desc.product_id() == CLASSIC_DFU,
        };
        found.push((device, info));
    }
    Ok(found)
}

/// Enumerate Apple DFU, recovery, and Classic disk-mode devices.
pub fn discover() -> Result<Vec<DeviceInfo>> {
    let context = Context::new().map_err(|e| usb_error("context", e))?;
    Ok(candidates(&context)?
        .into_iter()
        .map(|(_, info)| info)
        .collect())
}

pub(crate) struct UsbTransport {
    pub(crate) handle: DeviceHandle<Context>,
    pub(crate) timeout: Duration,
}

impl Transport for UsbTransport {
    fn read(&mut self, ty: u8, req: u8, val: u16, idx: u16, data: &mut [u8]) -> Result<usize> {
        self.handle
            .read_control(ty, req, val, idx, data, self.timeout)
            .map_err(|e| usb_error("control read", e))
    }

    fn write(&mut self, ty: u8, req: u8, val: u16, idx: u16, data: &[u8]) -> Result<usize> {
        self.handle
            .write_control(ty, req, val, idx, data, self.timeout)
            .map_err(|e| usb_error("control write", e))
    }
}

/// Claimed USB handle for sequential device operations.
pub struct Session {
    pub(crate) info: DeviceInfo,
    pub(crate) dfu: Dfu<UsbTransport>,
    checks_started: bool,
    pub(crate) operation_ready: bool,
    pub(crate) verified_rom: Option<Vec<u8>>,
}

impl Session {
    /// Claim the selected DFU device, or the sole connected candidate.
    pub fn open(selector: Option<DeviceSelector>) -> Result<Self> {
        let context = Context::new().map_err(|e| usb_error("context", e))?;
        let mut found: Vec<_> = candidates(&context)?
            .into_iter()
            .filter(|(_, info)| info.dfu_candidate && selector.is_none_or(|s| s == info.selector))
            .collect();
        if found.len() != 1 {
            return Err(Error::DeviceCount(found.len()));
        }
        let (device, info) = found.remove(0);
        let config = device
            .active_config_descriptor()
            .map_err(|e| usb_error("active configuration", e))?;
        let is_dfu = config.interfaces().flat_map(|i| i.descriptors()).any(|d| {
            d.interface_number() == 0
                && d.setting_number() == 0
                && d.class_code() == 0xfe
                && d.sub_class_code() == 1
                && d.protocol_code() == 2
        });
        if !is_dfu {
            return Err(invalid(
                "Expected BootROM DFU interface 0, alternate setting 0",
            ));
        }
        let handle = device
            .open()
            .map_err(|e| usb_error("open (check USB permissions/driver)", e))?;
        handle
            .claim_interface(0)
            .map_err(|e| usb_error("claim interface 0 (close other USB tools)", e))?;
        Ok(Self {
            info,
            dfu: Dfu {
                transport: UsbTransport {
                    handle,
                    timeout: TIMEOUT,
                },
            },
            checks_started: false,
            operation_ready: false,
            verified_rom: None,
        })
    }

    pub fn device(&self) -> &DeviceInfo {
        &self.info
    }

    /// Read and cross-check 1 MiB of NOR.
    /// Callback errors cancel with DFU cleanup.
    pub fn dump_nor(
        &mut self,
        emit: impl FnMut(crate::NorProgress) -> Result<()>,
    ) -> Result<crate::NorDump> {
        if !self.operation_ready {
            return Err(invalid(
                "NOR dumping requires successful checks on this session",
            ));
        }
        self.operation_ready = false;
        self.dfu.transport.timeout = Duration::from_secs(2);
        let result = self.dfu.dump_nor_checked(emit);
        self.dfu.transport.timeout = TIMEOUT;
        self.operation_ready = result.is_ok();
        result
    }

    pub fn return_to_idle(&mut self) -> Result<()> {
        let result = self.dfu.idle();
        if result.is_err() {
            self.operation_ready = false;
        }
        result
    }

    /// Decrypt raw AES-CBC with GKEY.
    /// Callback errors cancel; plaintext is returned after DFU cleanup.
    pub fn decrypt(
        &mut self,
        ciphertext: &[u8],
        options: crate::DecryptOptions,
        reference: Option<&[u8]>,
        emit: impl FnMut(crate::DecryptProgress) -> Result<()>,
    ) -> Result<crate::Decrypted> {
        options.validate(ciphertext, reference)?;
        if !self.operation_ready {
            return Err(invalid(
                "Decryption requires successful checks on this session",
            ));
        }
        self.operation_ready = false;
        self.dfu.transport.timeout = Duration::from_secs(2);
        let result = self
            .dfu
            .decrypt_checked(ciphertext, options, reference, emit);
        self.dfu.transport.timeout = TIMEOUT;
        self.operation_ready = result.is_ok();
        result
    }

    /// Decrypt the Apple NOR loader with UKEY after successful device checks.
    pub fn decrypt_nor(
        &mut self,
        nor: &[u8],
        mut emit: impl FnMut(crate::DecryptProgress) -> Result<()>,
    ) -> Result<Vec<u8>> {
        let image = crate::AppleNorImage::locate(nor)?;
        if !self.operation_ready {
            return Err(invalid(
                "NOR decryption requires successful checks on this session",
            ));
        }
        self.operation_ready = false;
        self.dfu.transport.timeout = Duration::from_secs(2);
        let result = image.decrypt(|cipher| {
            self.dfu
                .decrypt_key_checked(
                    cipher,
                    crate::DecryptOptions::default(),
                    None,
                    2,
                    |mut progress| {
                        progress.stage = if cipher.len() == 16 {
                            "nor_signature"
                        } else {
                            "nor_decrypt"
                        };
                        emit(progress)
                    },
                )
                .map(|output| output.plaintext)
        });
        self.dfu.transport.timeout = TIMEOUT;
        self.operation_ready = result.is_ok();
        result
    }

    /// Check DFU, BootROM, and SysCfg, then clean up. Runs once per session.
    pub fn check(&mut self, emit: impl FnMut(Event)) -> CheckReport {
        self.verified_rom = None;
        if self.checks_started {
            self.operation_ready = false;
            let mut report = CheckReport::new("live", Some(self.info.clone()));
            let mut emit = emit;
            report.set(
                CheckId::Dfu,
                CheckStatus::Failed,
                "Checks already ran; open a new session to check again",
                &mut emit,
            );
            report.skip_pending(&mut emit);
            return report;
        }
        self.checks_started = true;
        self.dfu.transport.timeout = Duration::from_secs(2);
        let (report, rom) = run_checks(&mut self.dfu, self.info.clone(), emit);
        self.verified_rom = rom;
        self.dfu.transport.timeout = TIMEOUT;
        self.operation_ready = report.compatible && report.cleanup == Cleanup::Idle;
        report
    }
}

pub(crate) fn run_checks<T: Transport>(
    dfu: &mut Dfu<T>,
    info: DeviceInfo,
    mut emit: impl FnMut(Event),
) -> (CheckReport, Option<Vec<u8>>) {
    let mut verified_rom = None;
    let mut report = CheckReport::new("live", Some(info));
    report.set(
        CheckId::Usb,
        CheckStatus::Passed,
        "05ac:1223 (shared Classic/Nano 3G ID)",
        &mut emit,
    );
    report.set(
        CheckId::Access,
        CheckStatus::Passed,
        "USB interface 0 claimed",
        &mut emit,
    );
    let mut stage = CheckId::Dfu;
    let mut probes_started = false;
    let result = (|| -> Result<()> {
        report.set(
            stage,
            CheckStatus::Running,
            "Checking DFU state and recovering protocol idle if needed",
            &mut emit,
        );
        if let Err(error) = dfu.idle() {
            report.cleanup = Cleanup::Failed(error.to_string());
            return Err(invalid(format!(
                "DFU recovery failed: {error}; re-enter BootROM DFU and try again"
            )));
        }
        report.set(
            stage,
            CheckStatus::Passed,
            "dfuIDLE; ready for probes",
            &mut emit,
        );
        stage = CheckId::Rom;
        report.set(
            stage,
            CheckStatus::Running,
            "Checking fast-reply routines, then reading 64 KiB BootROM in 512-byte chunks",
            &mut emit,
        );
        probes_started = true;
        let rom = dfu.bootrom(|completed| {
            emit(Event::Progress {
                stage: CheckId::Rom,
                completed,
                total: BOOTROM_SIZE,
            })
        })?;
        report.record_rom(&rom, &mut emit)?;
        verified_rom = Some(rom);
        stage = CheckId::Model;
        report.set(
            stage,
            CheckStatus::Running,
            "Reading and cross-checking NOR SysCfg",
            &mut emit,
        );
        let config = dfu.syscfg()?;
        report.record_identity(&config, &mut emit)
    })();
    if let Err(error) = result {
        report.set(stage, CheckStatus::Failed, error.to_string(), &mut emit);
    }
    report.skip_pending(&mut emit);
    if probes_started {
        report.cleanup = match dfu.idle() {
            Ok(()) => Cleanup::Idle,
            Err(error) => {
                report.compatible = false;
                Cleanup::Failed(error.to_string())
            }
        };
    }
    if !report.compatible || report.cleanup != Cleanup::Idle {
        verified_rom = None;
    }
    (report, verified_rom)
}
