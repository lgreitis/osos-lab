# SPDX-License-Identifier: GPL-3.0-only
"""Recipe construction is independent of Apple bytes; Rust checks preimages."""

import struct
import unittest
from unittest.mock import patch

from patching import declarations, rvx_runtime, symbols
from patching.declarations import Copy, Kind, Write
from patching.recipes import fingerprint, osos


def word(kind, address, expected, symbol="", value=0):
    return Write(kind, address, struct.pack("<I", expected), value, symbol)


class OsosTests(unittest.TestCase):
    def test_patch_symbols_preserve_local_targets_but_reject_ambiguous_names(self):
        listing = "\n".join(
            [
                "1: 08b3c001 4 FUNC LOCAL DEFAULT 1 thumb_hook",
                "2: 08b3c010 4 OBJECT LOCAL DEFAULT 1 table",
                "3: 08b3c020 4 OBJECT LOCAL DEFAULT 1 table",
                "4: 08b3c030 4 OBJECT LOCAL DEFAULT 1 unique_table",
                "5: 08b3c040 4 OBJECT GLOBAL DEFAULT 1 global_table",
                "6: 08b3c040 4 OBJECT GLOBAL DEFAULT 1 global_table",
            ]
        )
        with patch("patching.symbols.subprocess.check_output", return_value=listing):
            linked = symbols.read("payload.elf", "arm-")
            self.assertEqual(symbols.require(linked, "thumb_hook"), 0x08B3C001)
            self.assertEqual(symbols.require(linked, "unique_table"), 0x08B3C030)
            self.assertEqual(symbols.require(linked, "global_table"), 0x08B3C040)
            with self.assertRaisesRegex(ValueError, "Ambiguous.*table"):
                symbols.require(linked, "table")
            with self.assertRaisesRegex(ValueError, "Missing.*absent"):
                symbols.require(linked, "absent")
            linked = symbols.read("payload.elf", "arm-", thumb_bits=False)
            self.assertEqual(symbols.require(linked, "thumb_hook"), 0x08B3C000)

    def setUp(self):
        self.load_offset = 0xB798
        self.original = bytearray(self.load_offset + 256)
        self.original[self.load_offset : self.load_offset + 8] = b"native!!"
        self.code = bytes(128)
        self.symbols = {
            "__osos_load_offset": self.load_offset,
            "__payload_start": 0x08B3C000,
            "__payload_end": 0x08B3C080,
            "__payload_limit": 0x08B3C100,
            "__payload_file_offset": len(self.original),
            "hook": 0x08B3C000,
            "table": 0x08B3C020,
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
            image[self.load_offset : self.load_offset + 16].hex(),
            "20c0b308fdef2ceb04f01fe500c0b308",
        )
        self.assertEqual(
            recipe.checks[0],
            {"name": "osos", "offset": self.load_offset, "hex": "78563412"},
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

    def test_dynamic_reservation_includes_bss_and_matches_heap_and_file_size(self):
        start = self.symbols["__payload_start"]
        self.symbols["__payload_end"] = start + 332140
        self.symbols["__payload_limit"] = start + 328 * 1024
        self.code = b"CFW!" + bytes(124)
        image = self.replay(
            self.build([word(Kind.POINTER, osos.BASE, 0, "__payload_limit")])
        )
        self.assertEqual(len(image), len(self.original) + 328 * 1024)
        self.assertEqual(
            struct.unpack_from("<I", image, self.load_offset)[0], start + 328 * 1024
        )
        self.assertEqual(
            image[len(self.original) : len(self.original) + len(self.code)], self.code
        )
        self.assertEqual(
            image[len(self.original) + len(self.code) :],
            bytes(328 * 1024 - len(self.code)),
        )
        for offset in osos.IMAGE_SIZE_FIELDS:
            self.assertEqual(
                struct.unpack_from("<I", image, offset)[0], len(image) - 0x800
            )

    def test_runtime_relocations_preserve_thumb_bits_and_fixed_addresses(self):
        base = 0x10000
        raw = struct.pack("<3I", 5, 16, 0x08000000)
        mirror = struct.pack("<3I", base + 5, base + 16, 0x08000000)
        self.assertEqual(rvx_runtime.relocation_words(raw, mirror, base, 16), [0, 1])
        with self.assertRaisesRegex(ValueError, "Non-relocatable"):
            rvx_runtime.relocation_words(raw, mirror, base, 15)

    def test_runtime_relocation_index_must_fit_in_trailer(self):
        padding = bytes(0x10000 * 4)
        raw = padding + struct.pack("<I", 4)
        mirror = padding + struct.pack("<I", 0x10004)
        with self.assertRaisesRegex(ValueError, "16-bit index"):
            rvx_runtime.relocation_words(raw, mirror, 0x10000, len(raw))

    def test_runtime_trailer_follows_the_resident_reservation(self):
        trailer = b"runtime"
        patches = [word(Kind.POINTER, osos.BASE, 0, "__payload_limit")]
        recipe = osos.build(self.code, patches, self.symbols, self.fingerprint, trailer)
        image = self.replay(recipe)
        resident = self.symbols["__payload_limit"] - self.symbols["__payload_start"]
        self.assertEqual(image[len(self.original) + resident :], trailer)
        self.assertEqual(
            struct.unpack_from("<I", image, self.load_offset)[0],
            self.symbols["__payload_limit"],
        )
        for offset in osos.IMAGE_SIZE_FIELDS:
            self.assertEqual(
                struct.unpack_from("<I", image, offset)[0], len(image) - 0x800
            )

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
        for destination in (0x08B3BFFC, 0x08B3C001, 0x08B3C080, 0x08B3C100):
            self.symbols["hook"] = destination
            with self.subTest(destination=destination), self.assertRaises(ValueError):
                self.build([patch])
        for name in ("hook", "__osos_load_offset"):
            value = self.symbols.pop(name)
            with (
                self.subTest(name=name),
                self.assertRaisesRegex(
                    ValueError, "Missing linked patch symbol: " + name
                ),
            ):
                self.build([patch])
            self.symbols[name] = value

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
