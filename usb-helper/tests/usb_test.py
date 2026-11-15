# SPDX-License-Identifier: GPL-3.0-only
"""Check the helper's Microsoft OS descriptor encoding and request routing."""

import ctypes
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]


class UsbTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        source = root / "usb.c"
        source.write_text("""
#include "winusb.h"
#include <string.h>
unsigned get_descriptor(unsigned type, unsigned req, unsigned value, unsigned index, void *out) {
    const void *data;
    unsigned size = upload_os_descriptor(type, req, value, index, &data);
    if (size) memcpy(out, data, size);
    return size;
}
""")
        helper = (HERE / "usb.c").read_text()
        control = helper[
            helper.index("static bool control(") : helper.index("/* Replaces Rockbox")
        ]
        with source.open("a") as stream:
            stream.write(
                """
#include "layout.h"
#include "upload.h"
#include <stdbool.h>
struct usb_ctrlrequest { unsigned bRequestType, bRequest, wValue, wIndex, wLength; };
static unsigned interface_number, activity, return_at;
static bool finished, start_pending, next_pending;
static struct upload_config next_config;
static struct upload_result result;
#undef UPLOAD_RESULT
#define UPLOAD_RESULT (&result)
#define USEC_TIMER 123
#define USB_CONTROL_ACK 0
static uint8_t status_data[320], probe_data[sizeof(struct layout_attempt)];
struct storage_probe upload_probe;
struct storage_diagnostics upload_diagnostics;
struct layout_attempt upload_attempts[LAYOUT_ATTEMPTS];
static unsigned response_size;
static uint8_t response[sizeof(struct layout_attempt)];
void upload_fail(int rc) { (void)rc; }
static void usb_core_control_response(int ack, const void *data, unsigned size) {
    (void)ack;
    response_size = size;
    if (size) memcpy(response, data, size);
}
"""
                + control
                + """
unsigned probe_request(unsigned type, unsigned request, unsigned value,
                       unsigned index, unsigned length, void *out) {
    struct usb_ctrlrequest req = {type, request, value, index, length};
    upload_probe.count = 2;
    upload_attempts[1].lba = 394224;
    memset(upload_attempts[1].sectors[0], 0x55, 512);
    memset(upload_attempts[1].sectors[1], 0xaa, 512);
    uint8_t nonce[16] = {0};
    response_size = 0;
    if (!control(&req, nonce, sizeof(nonce))) return 0;
    memcpy(out, response, response_size);
    return response_size;
}
/* Returns 1 when the request was claimed, plus 2 when a next file was queued and 4
   when START is pending. */
unsigned command_request(unsigned request, unsigned length, unsigned state, int rc,
                         const void *payload) {
    struct usb_ctrlrequest req = {0x21, request, 0, 0, length};
    uint8_t data[256] = {0};
    memcpy(data, payload, length);
    result.state = state;
    result.rc = rc;
    memset(result.nonce, 7, 16);
    finished = false;
    start_pending = false;
    next_pending = false;
    memset(&next_config, 0, sizeof(next_config));
    unsigned claimed = control(&req, data, sizeof(data)) ? 1 : 0;
    if (next_pending && !memcmp(next_config.path, "/a.bin", 7)) claimed |= 2;
    if (start_pending) claimed |= 4;
    return claimed;
}
"""
            )
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-shared",
                "-fPIC",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I" + str(HERE),
                str(source),
                "-o",
                str(root / "usb.so"),
            ],
            check=True,
        )
        cls.lib = ctypes.CDLL(str(root / "usb.so"))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def descriptor(self, type, request, value, index):
        output = ctypes.create_string_buffer(256)
        size = self.lib.get_descriptor(type, request, value, index, output)
        return output.raw[:size]

    def test_probe_requests_preserve_legacy_sizes_and_bound_attempt_indices(self):
        output = ctypes.create_string_buffer(1056)
        request = self.lib.probe_request
        self.assertEqual(request(0xA1, 0x52, 0, 0, 320, output), 320)
        self.assertEqual(request(0xA1, 0x56, 0, 0, 312, output), 312)
        self.assertEqual(request(0xA1, 0x57, 0, 0, 96, output), 96)
        self.assertEqual(struct.unpack_from("<I", output, 8)[0], 2)
        self.assertEqual(request(0xA1, 0x58, 1, 0, 1056, output), 1056)
        self.assertEqual(struct.unpack_from("<I", output, 8)[0], 394224)
        self.assertEqual(output.raw[32:544], bytes([0x55]) * 512)
        self.assertEqual(output.raw[544:], bytes([0xAA]) * 512)
        for type_, req, value, index, length in [
            (0xA1, 0x58, 2, 0, 1056),
            (0xA1, 0x58, 1, 1, 1056),
            (0xA1, 0x58, 1, 0, 512),
            (0x21, 0x58, 1, 0, 1056),
            (0xA1, 0x57, 1, 0, 96),
        ]:
            self.assertEqual(request(type_, req, value, index, length, output), 0)

    def test_next_file_needs_a_complete_session_and_the_full_config(self):
        nonce = bytes([7]) * 16
        config = (
            nonce
            + struct.pack("<II", 512, 1)
            + bytes(32)
            + b"/a.bin".ljust(192, b"\0")
        )
        self.assertEqual(len(config), 248)
        request = self.lib.command_request
        self.assertEqual(request(0x59, 248, 4, 0, config), 3)
        self.assertEqual(request(0x53, 16, 1, 0, nonce), 5)
        self.assertEqual(request(0x54, 16, 4, 0, nonce), 1)
        for req, length, state, rc, payload in [
            (0x59, 248, 1, 0, config),
            (0x59, 248, 4, -304, config),
            (0x59, 247, 4, 0, config),
            (0x59, 16, 4, 0, config),
            (0x59, 248, 4, 0, bytes(16) + config[16:]),
            (0x53, 248, 1, 0, config),
            (0x53, 16, 4, 0, nonce),
            (0x54, 15, 4, 0, nonce),
        ]:
            self.assertEqual(request(req, length, state, rc, payload), 0, (req, length))

    def test_os_string_and_compatible_id(self):
        string = self.descriptor(0x80, 6, 0x03EE, 0)
        self.assertEqual(string[:2], bytes([18, 3]))
        self.assertEqual(string[2:16].decode("utf-16le"), "MSFT100")
        self.assertEqual(string[16:], bytes([0x51, 0]))
        compat = self.descriptor(0xC0, 0x51, 0, 4)
        self.assertEqual(len(compat), 40)
        self.assertEqual(struct.unpack_from("<IHHB", compat), (40, 0x100, 4, 1))
        self.assertEqual(compat[16:26], b"\0\1WINUSB\0\0")

    def test_guid_property_lengths_and_contents(self):
        data = self.descriptor(0xC0, 0x51, 0, 5)
        self.assertEqual(data, self.descriptor(0xC1, 0x51, 0, 5))
        self.assertEqual(len(data), 142)
        self.assertEqual(struct.unpack_from("<IHHH", data), (142, 0x100, 5, 1))
        self.assertEqual(struct.unpack_from("<IIH", data, 10), (132, 1, 40))
        self.assertEqual(data[20:60].decode("utf-16le"), "DeviceInterfaceGUID\0")
        self.assertEqual(struct.unpack_from("<I", data, 60), (78,))

    def test_unrelated_control_requests_are_not_claimed(self):
        for request in [
            (0x80, 6, 0x0301, 0),
            (0x40, 0x51, 0, 4),
            (0xC1, 0x51, 1, 5),
            (0xC0, 0x52, 0, 4),
            (0xA1, 0x52, 0, 0),
        ]:
            self.assertEqual(self.descriptor(*request), b"")


if __name__ == "__main__":
    unittest.main()
