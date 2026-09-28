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
