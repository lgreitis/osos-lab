# SPDX-License-Identifier: GPL-3.0-only
"""Offline partition and FAT32 boundary checks."""

import ctypes
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]


class LayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        source = (HERE / "main.c").read_text()
        validator = source[
            source.index("static bool file_layout_ok") : source.index(
                "static void return_to_dfu"
            )
        ]
        (root / "layout.c").write_text(
            """
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
static struct { uint32_t sectors_high, sectors_low; } record;
#define RECORD (&record)
static struct { uint8_t sectors[5][512]; uint32_t lba[4], mask; } layout;
static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
"""
            + validator
            + """
int validate(const uint8_t *mbr, const uint8_t *bpb, uint64_t capacity, unsigned slot) {
    memset(&layout, 0, sizeof(layout));
    record.sectors_low = capacity;
    record.sectors_high = capacity >> 32;
    memcpy(layout.sectors[0], mbr, 512);
    memcpy(layout.sectors[slot+1], bpb, 512);
    layout.mask = 1 | (1u << (slot+1));
    layout.lba[slot] = le32(mbr+454+slot*16) * (bpb[11] | (unsigned)bpb[12]<<8) / 512;
    uint64_t start, end; unsigned bytes;
    return file_layout_ok(&start, &end, &bytes);
}
"""
        )
        library = root / "layout.so"
        subprocess.run(
            [
                "cc",
                "-shared",
                "-fPIC",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(root / "layout.c"),
                "-o",
                str(library),
            ],
            check=True,
        )
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.validate.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_uint64,
            ctypes.c_uint,
        ]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def image(self, bytes_per_sector=512, slot=0):
        mbr, bpb = bytearray(512), bytearray(512)
        mbr[510:] = bpb[510:] = b"\x55\xaa"
        mbr[450 + slot * 16] = 0x0C
        struct.pack_into("<II", mbr, 454 + slot * 16, 2048, 2000000)
        struct.pack_into("<HBHB", bpb, 11, bytes_per_sector, 4, 32, 2)
        struct.pack_into("<II", bpb, 32, 2000000, 4000)
        struct.pack_into("<I", bpb, 44, 2)
        return mbr, bpb

    def valid(self, mbr, bpb, slot=0, capacity=20000000):
        return bool(self.lib.validate(bytes(mbr), bytes(bpb), capacity, slot))

    def test_multiple_sector_sizes_and_partition_slots(self):
        for size in [512, 1024, 2048, 4096]:
            for slot in range(4):
                self.assertTrue(self.valid(*self.image(size, slot), slot=slot))

    def test_rejects_overlap_capacity_and_invalid_fat_geometry(self):
        mbr, bpb = self.image()
        self.assertFalse(self.valid(mbr, bpb, capacity=1000000))
        mbr[466] = 0x3F
        struct.pack_into("<II", mbr, 470, 2040, 16)
        self.assertFalse(self.valid(mbr, bpb))
        for offset, value in [(13, 3), (16, 0), (17, 1), (22, 1), (42, 1), (510, 0)]:
            mbr, bpb = self.image()
            bpb[offset] = value
            self.assertFalse(self.valid(mbr, bpb), offset)
        for offset, value in [(32, 3000000), (36, 1), (44, 1), (44, 3000000)]:
            mbr, bpb = self.image()
            struct.pack_into("<I", bpb, offset, value)
            self.assertFalse(self.valid(mbr, bpb), offset)
