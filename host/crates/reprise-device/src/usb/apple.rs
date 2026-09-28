// SPDX-License-Identifier: GPL-3.0-only
// Apple PIPE0 protocol follows Rockbox utils/mks5lboot/ipoddfu.c
// (Copyright 2015 Castor Munoz, GPL-2.0-or-later).

use super::{
    apple_packet::{packet, payload_length, HEADER_SIZE},
    usb_error,
};
use crate::{invalid, Error, Result};
use std::{
    io,
    mem::size_of,
    os::windows::io::{AsRawHandle, FromRawHandle, OwnedHandle},
    ptr::{null, null_mut},
    time::Duration,
};
use windows_sys::{
    core::GUID,
    Win32::{
        Devices::DeviceAndDriverInstallation::*,
        Foundation::{
            GetLastError, ERROR_DEVICE_NOT_CONNECTED, ERROR_FILE_NOT_FOUND,
            ERROR_INSUFFICIENT_BUFFER, ERROR_IO_PENDING, ERROR_NO_MORE_ITEMS, ERROR_NO_SUCH_DEVICE,
            ERROR_SEM_TIMEOUT, GENERIC_READ, GENERIC_WRITE, INVALID_HANDLE_VALUE, WAIT_TIMEOUT,
        },
        Storage::FileSystem::{
            CreateFileW, ReadFile, WriteFile, FILE_FLAG_OVERLAPPED, FILE_SHARE_READ,
            FILE_SHARE_WRITE, OPEN_EXISTING,
        },
        System::{
            Threading::CreateEventW,
            IO::{CancelIoEx, GetOverlappedResult, GetOverlappedResultEx, OVERLAPPED},
        },
    },
};

const APPLE_DFU: GUID = GUID::from_u128(0xb8085869_feb9_404b_8cb1_1e5c14fa8c54);

struct DeviceList(HDEVINFO);

impl Drop for DeviceList {
    fn drop(&mut self) {
        unsafe {
            SetupDiDestroyDeviceInfoList(self.0);
        }
    }
}

fn error(operation: &'static str, code: u32) -> Error {
    match code {
        WAIT_TIMEOUT | ERROR_SEM_TIMEOUT => usb_error(operation, rusb::Error::Timeout),
        // Apple's driver can report FILE_NOT_FOUND when the iPod leaves DFU.
        ERROR_DEVICE_NOT_CONNECTED | ERROR_NO_SUCH_DEVICE | ERROR_FILE_NOT_FOUND => {
            usb_error(operation, rusb::Error::NoDevice)
        }
        _ => invalid(format!(
            "USB {operation}: {}",
            io::Error::from_raw_os_error(code as i32)
        )),
    }
}

fn paths() -> Result<Vec<Vec<u16>>> {
    let list = unsafe {
        SetupDiGetClassDevsW(
            &APPLE_DFU,
            null(),
            null_mut(),
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE,
        )
    };
    if list == INVALID_HANDLE_VALUE as HDEVINFO {
        return Err(error("Apple DFU enumeration", unsafe { GetLastError() }));
    }
    let list = DeviceList(list);
    let mut paths = Vec::new();
    for index in 0.. {
        let mut interface = SP_DEVICE_INTERFACE_DATA {
            cbSize: size_of::<SP_DEVICE_INTERFACE_DATA>() as u32,
            ..Default::default()
        };
        if unsafe { SetupDiEnumDeviceInterfaces(list.0, null(), &APPLE_DFU, index, &mut interface) }
            == 0
        {
            let code = unsafe { GetLastError() };
            if code == ERROR_NO_MORE_ITEMS {
                break;
            }
            return Err(error("Apple DFU interface enumeration", code));
        }
        let mut required = 0;
        unsafe {
            SetupDiGetDeviceInterfaceDetailW(
                list.0,
                &interface,
                null_mut(),
                0,
                &mut required,
                null_mut(),
            );
        }
        let code = unsafe { GetLastError() };
        if code != ERROR_INSUFFICIENT_BUFFER
            || required < size_of::<SP_DEVICE_INTERFACE_DETAIL_DATA_W>() as u32
        {
            return Err(error("Apple DFU interface size", code));
        }
        // A DWORD-aligned allocation accommodates the variable-length UTF-16 path.
        let mut storage = vec![0u32; (required as usize).div_ceil(4)];
        let detail = storage
            .as_mut_ptr()
            .cast::<SP_DEVICE_INTERFACE_DETAIL_DATA_W>();
        unsafe {
            (*detail).cbSize = size_of::<SP_DEVICE_INTERFACE_DETAIL_DATA_W>() as u32;
        }
        if unsafe {
            SetupDiGetDeviceInterfaceDetailW(
                list.0,
                &interface,
                detail,
                required,
                null_mut(),
                null_mut(),
            )
        } == 0
        {
            return Err(error("Apple DFU interface path", unsafe { GetLastError() }));
        }
        let path = unsafe {
            std::slice::from_raw_parts(
                std::ptr::addr_of!((*detail).DevicePath).cast::<u16>(),
                (required as usize - 4) / 2,
            )
        };
        let length = path
            .iter()
            .position(|c| *c == 0)
            .ok_or_else(|| invalid("Unterminated Apple DFU device path"))?;
        if String::from_utf16_lossy(&path[..length])
            .to_ascii_lowercase()
            .starts_with(r"\\?\usb#vid_05ac&pid_1223#")
        {
            paths.push(path[..length].to_vec());
        }
    }
    Ok(paths)
}

fn open_handle(path: &[u16], flags: u32) -> Result<OwnedHandle> {
    let path: Vec<_> = path.iter().copied().chain([0]).collect();
    let handle = unsafe {
        CreateFileW(
            path.as_ptr(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            null(),
            OPEN_EXISTING,
            flags,
            null_mut(),
        )
    };
    if handle == INVALID_HANDLE_VALUE {
        return Err(error(
            "Apple DFU open (close iTunes and other USB tools)",
            unsafe { GetLastError() },
        ));
    }
    Ok(unsafe { OwnedHandle::from_raw_handle(handle) })
}

pub(super) struct AppleDfu {
    pipe: OwnedHandle,
    _device: OwnedHandle,
}

impl AppleDfu {
    pub(super) fn open(dfu_count: usize) -> Result<Option<Self>> {
        let mut paths = paths()?;
        if paths.is_empty() {
            return Ok(None);
        }
        // Native Apple paths and libusb bus addresses have no shared stable key.
        if dfu_count != 1 || paths.len() != 1 {
            return Err(invalid(
                "Connect only one DFU iPod when using the Apple DFU driver",
            ));
        }
        let mut path = paths.pop().unwrap();
        let device = open_handle(&path, 0)?;
        path.extend("\\PIPE0".encode_utf16());
        let pipe = open_handle(&path, FILE_FLAG_OVERLAPPED)?;
        Ok(Some(Self {
            pipe,
            _device: device,
        }))
    }

    fn transfer(&mut self, packet: &mut [u8], read: bool, timeout: Duration) -> Result<usize> {
        let operation = if read {
            "Apple DFU control read"
        } else {
            "Apple DFU control write"
        };
        let event = unsafe { CreateEventW(null(), 1, 0, null()) };
        if event.is_null() {
            return Err(error(operation, unsafe { GetLastError() }));
        }
        let event = unsafe { OwnedHandle::from_raw_handle(event) };
        let mut overlapped = OVERLAPPED {
            hEvent: event.as_raw_handle(),
            ..Default::default()
        };
        let handle = self.pipe.as_raw_handle();
        let mut transferred = 0;
        let result = unsafe {
            if read {
                ReadFile(
                    handle,
                    packet.as_mut_ptr(),
                    packet.len() as u32,
                    &mut transferred,
                    &mut overlapped,
                )
            } else {
                WriteFile(
                    handle,
                    packet.as_ptr(),
                    packet.len() as u32,
                    &mut transferred,
                    &mut overlapped,
                )
            }
        };
        if result == 0 {
            let code = unsafe { GetLastError() };
            if code != ERROR_IO_PENDING {
                return Err(error(operation, code));
            }
            if unsafe {
                GetOverlappedResultEx(
                    handle,
                    &overlapped,
                    &mut transferred,
                    timeout.as_millis().clamp(1, u128::from(u32::MAX - 1)) as u32,
                    0,
                )
            } == 0
            {
                let code = unsafe { GetLastError() };
                // Keep the packet and OVERLAPPED alive until cancellation completes.
                unsafe {
                    CancelIoEx(handle, &overlapped);
                    GetOverlappedResult(handle, &overlapped, &mut transferred, 1);
                }
                return Err(error(operation, code));
            }
        }
        payload_length(transferred as usize, packet.len() - HEADER_SIZE)
    }

    pub(super) fn read(
        &mut self,
        ty: u8,
        request: u8,
        value: u16,
        index: u16,
        data: &mut [u8],
        timeout: Duration,
    ) -> Result<usize> {
        let mut wire = packet(ty, request, value, index, data.len())?;
        let size = self.transfer(&mut wire, true, timeout)?;
        data[..size].copy_from_slice(&wire[HEADER_SIZE..HEADER_SIZE + size]);
        Ok(size)
    }

    pub(super) fn write(
        &mut self,
        ty: u8,
        request: u8,
        value: u16,
        index: u16,
        data: &[u8],
        timeout: Duration,
    ) -> Result<usize> {
        let mut wire = packet(ty, request, value, index, data.len())?;
        wire[HEADER_SIZE..].copy_from_slice(data);
        self.transfer(&mut wire, false, timeout)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn missing_apple_pipe_is_a_usb_disconnect() {
        assert!(matches!(
            error("Apple DFU control read", ERROR_FILE_NOT_FOUND),
            Error::Usb {
                source: rusb::Error::NoDevice,
                ..
            }
        ));
    }
}
