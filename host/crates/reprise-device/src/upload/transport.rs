// SPDX-License-Identifier: GPL-3.0-only
use super::{
    image::UsbIdentity,
    protocol::{Status, RESULT_SIZE, STATUS},
};
use crate::{exact, invalid, usb::usb_error, DeviceInfo, Result};
use rusb::{Context, DeviceHandle, UsbContext};
use std::time::{Duration, Instant};
const TIMEOUT: Duration = Duration::from_secs(15);

pub(super) struct Bulk {
    handle: DeviceHandle<Context>,
    endpoint: u8,
}
pub(super) trait BulkTransport {
    fn endpoint(&self) -> u8;
    fn status(&mut self, nonce: &[u8; 16], size: u32) -> Result<Status>;
    fn command(&mut self, request: u8, nonce: &[u8; 16]) -> Result<()>;
    fn send(&mut self, data: &[u8]) -> Result<usize>;
}

impl BulkTransport for Bulk {
    fn endpoint(&self) -> u8 {
        self.endpoint
    }

    fn send(&mut self, data: &[u8]) -> Result<usize> {
        self.handle
            .write_bulk(self.endpoint, data, TIMEOUT)
            .map_err(|e| usb_error("file upload", e))
    }

    fn status(&mut self, nonce: &[u8; 16], size: u32) -> Result<Status> {
        let mut b = [0; RESULT_SIZE];
        let n = self
            .handle
            .read_control(0xa1, STATUS, 0, 0, &mut b, TIMEOUT)
            .map_err(|e| usb_error("upload status", e))?;
        Status::parse(&b[..n], nonce, size)
    }

    fn command(&mut self, request: u8, nonce: &[u8; 16]) -> Result<()> {
        let n = self
            .handle
            .write_control(0x21, request, 0, 0, nonce, TIMEOUT)
            .map_err(|e| usb_error("upload command", e))?;
        exact("Upload command", n, 16)
    }
}

pub(super) fn same_port(a: &DeviceInfo, bus: u8, ports: &[u8]) -> bool {
    a.selector.bus == bus && a.port_path == ports && !ports.is_empty()
}

fn driver_pending(error: rusb::Error) -> bool {
    cfg!(windows)
        && matches!(
            error,
            rusb::Error::NotSupported
                | rusb::Error::NoDevice
                | rusb::Error::Access
                | rusb::Error::Io
                | rusb::Error::NotFound
        )
}

pub(super) fn open_bulk(info: &DeviceInfo, usb: UsbIdentity) -> Result<Bulk> {
    let context = Context::new().map_err(|e| usb_error("upload context", e))?;
    let deadline = Instant::now() + Duration::from_secs(90);
    let mut last_error = None;
    loop {
        for device in context
            .devices()
            .map_err(|e| usb_error("upload enumeration", e))?
            .iter()
        {
            if !same_port(
                info,
                device.bus_number(),
                &device
                    .port_numbers()
                    .map_err(|e| usb_error("upload port", e))?,
            ) {
                continue;
            }
            let desc = device
                .device_descriptor()
                .map_err(|e| usb_error("upload descriptor", e))?;
            if desc.vendor_id() != usb.vendor_id || desc.product_id() != usb.product_id {
                continue;
            }
            let config = match device.active_config_descriptor() {
                Ok(config) => config,
                Err(error) if driver_pending(error) => {
                    last_error = Some(error);
                    continue;
                }
                Err(error) => return Err(usb_error("upload configuration", error)),
            };
            let interface = config
                .interfaces()
                .flat_map(|i| i.descriptors())
                .find(|d| {
                    d.interface_number() == 0
                        && d.setting_number() == 0
                        && d.class_code() == 0xff
                        && d.sub_class_code() == 0x52
                        && d.protocol_code() == 2
                        && d.num_endpoints() == 1
                })
                .ok_or_else(|| invalid("Unexpected upload USB interface"))?;
            let ep = interface.endpoint_descriptors().next().unwrap();
            if ep.direction() != rusb::Direction::Out
                || ep.transfer_type() != rusb::TransferType::Bulk
            {
                return Err(invalid("Unexpected upload endpoint"));
            }
            let handle = match device.open().and_then(|handle| {
                handle.claim_interface(0)?;
                Ok(handle)
            }) {
                Ok(handle) => handle,
                Err(error) if driver_pending(error) => {
                    last_error = Some(error);
                    continue;
                }
                Err(error) => return Err(usb_error("upload open/claim", error)),
            };
            return Ok(Bulk {
                handle,
                endpoint: ep.address(),
            });
        }
        if Instant::now() >= deadline {
            if let Some(error) = last_error {
                return Err(invalid(format!("Upload helper {:04x}:{:04x} appeared, but Windows could not open its WinUSB interface: {error}", usb.vendor_id, usb.product_id)));
            }
            return Err(invalid("Upload helper did not enumerate"));
        }
        std::thread::sleep(Duration::from_millis(100));
    }
}
