# SPDX-License-Identifier: GPL-3.0-only
"""Recipe construction is independent of Apple bytes; Rust checks preimages."""

import struct
import unittest

from patching import declarations, osos
from patching.declarations import Copy, Kind, Write
from patching.recipe import fingerprint


def word(kind, address, expected, symbol="", value=0):
    return Write(kind, address, struct.pack("<I", expected), value, symbol)


class OsosTests(unittest.TestCase):
    def setUp(self):
        self.original = bytearray(osos.LOAD_OFFSET + 256)
        self.original[osos.LOAD_OFFSET : osos.LOAD_OFFSET + 8] = b"native!!"
        self.code = bytes(128)
        self.symbols = {
            "__payload_start": 0x08B33000,
            "__payload_end": 0x08B33080,
            "__payload_limit": 0x08B33100,
            "__payload_file_offset": len(self.original),
            "hook": 0x08B33000,
            "table": 0x08B33020,
        }
        self.fingerprint = fingerprint(self.original)

    def build(self, patches):
        return osos.build(self.code, patches, self.symbols, self.fingerprint)

    def replay(self, recipe):
        output = bytearray()
        for segment in recipe.segments:
            size = segment["bytes"]
            if segment["kind"] == "zero":
                output.extend(bytes(size))
            else:
                source = self.original if segment["kind"] == "input" else recipe.data
                offset = segment["offset"]
                output.extend(source[offset : offset + size])
        self.assertEqual(len(output), recipe.size)
        return output

    def test_hooks_header_lengths_and_deferred_preimages(self):
        patches = [
            word(Kind.POINTER, osos.BASE, 0x12345678, "table"),
            word(Kind.CALL, osos.BASE + 4, 0xE350006B, "hook"),
            Write(
                Kind.JUMP,
                osos.BASE + 8,
                struct.pack("<II", 0xE2400064, 0xE3500015),
                symbol="hook",
            ),
        ]
        recipe = self.build(patches)
        image = self.replay(recipe)
        self.assertEqual(
            image[osos.LOAD_OFFSET : osos.LOAD_OFFSET + 16].hex(),
            "2030b308fdcb2ceb04f01fe50030b308",
        )
        self.assertEqual(
            recipe.checks[0],
            {"name": "osos", "offset": osos.LOAD_OFFSET, "hex": "78563412"},
        )
        for offset in (0xC, 0x10, 0x14):
            self.assertEqual(
                struct.unpack_from("<I", image, offset)[0], len(image) - 0x800
            )

    def test_native_array_copies_preserve_adjacent_literals(self):
        self.code = bytes(40) + b"CFW!" + bytes(84)
        recipe = self.build([Copy("table", 0, 12, osos.BASE, 8)])
        image = self.replay(recipe)
        self.assertEqual(
            image[len(self.original) + 32 : len(self.original) + 44], b"native!!CFW!"
        )
        self.assertNotIn(b"native!!", recipe.data)

    def test_rejects_overlap_and_invalid_write_ranges(self):
        valid = word(Kind.WORD, osos.BASE, 0, value=42)
        for patches in (
            [valid, valid],
            [word(Kind.WORD, osos.BASE - 4, 0)],
            [word(Kind.WORD, osos.BASE + 256, 0)],
            [word(Kind.WORD, osos.BASE + 1, 0)],
        ):
            with self.subTest(patches=patches), self.assertRaises(ValueError):
                self.build(patches)

    def test_rejects_missing_thumb_and_out_of_range_symbols(self):
        patch = word(Kind.CALL, osos.BASE, 0, "hook")
        for destination in (0x08B32FFC, 0x08B33001, 0x08B33080, 0x08B33100):
            self.symbols["hook"] = destination
            with self.subTest(destination=destination), self.assertRaises(ValueError):
                self.build([patch])
        del self.symbols["hook"]
        with self.assertRaisesRegex(ValueError, "Missing linked patch symbol: hook"):
            self.build([patch])

    def test_rejects_invalid_and_overlapping_copies(self):
        valid = Copy("table", 0, 16, osos.BASE, 8)
        cases = [
            [valid, valid],
            [Copy("table", 12, 16, osos.BASE, 8)],
            [Copy("table", 0, 16, osos.BASE - 1, 8)],
            [Copy("table", 0, 16, osos.BASE + 252, 8)],
            [Copy("table", 96, 128, osos.BASE, 8)],
        ]
        for patches in cases:
            with self.subTest(patches=patches), self.assertRaises(ValueError):
                self.build(patches)
        self.code = bytes(257)
        with self.assertRaisesRegex(ValueError, "reserved"):
            self.build([])

    def test_metadata_decodes_explicit_fields(self):
        raw = declarations.RECORD.pack(5, osos.BASE, 4, 12, 8, b"table")
        self.assertEqual(declarations.parse(raw), [Copy("table", 4, 12, osos.BASE, 8)])
        for data in (raw[:-1], bytes(declarations.RECORD.size), raw[:20] + b"x" * 64):
            with self.subTest(data=data), self.assertRaises(ValueError):
                declarations.parse(data)
        for destination in (osos.BASE + 1, osos.BASE + (1 << 25) + 8):
            with self.assertRaises(ValueError):
                osos.arm_call(osos.BASE, destination)
