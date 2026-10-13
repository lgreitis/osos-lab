# SPDX-License-Identifier: GPL-3.0-only
"""Native theme limits, lossless palette edits and reversible pack contracts."""

import contextlib
import io
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from patching.tests.test_bitmap import png
from patching.theme_format import HEADER, METADATA, RECORD, digest
from themes.compiler import (
    compile_theme,
    describe,
    encode_replacement,
    tint_icon,
)


def indexed(depth=8):
    pixels = bytes([0, 1, 0, 1]) if depth == 8 else struct.pack("<4H", 0, 1, 0, 1)
    body = struct.pack("<I", 2) + bytes([3, 2, 1, 255, 6, 5, 4, 127]) + pixels
    return (
        struct.pack(
            "<4H5I",
            0x64 if depth == 8 else 0x65,
            0,
            len(pixels),
            depth,
            0,
            0,
            1,
            4,
            len(body),
        )
        + body
    )


class ThemeTests(unittest.TestCase):
    def test_icon_tint_preserves_alpha_and_palette_budget(self):
        rgba = bytes([255, 255, 255, 255, 128, 128, 128, 127, 0, 0, 0, 0]) * 2
        for accent in (0xB57BE8, 0xE3B944, 0x42C8BB, 0xEB686D):
            tinted = tint_icon(rgba, accent)
            self.assertEqual(tinted[3::4], rgba[3::4])
            self.assertEqual(tinted[:3], accent.to_bytes(3, "big"))
            self.assertEqual(tinted[8:12], bytes(4))
            self.assertLessEqual(
                len(set(tinted[i : i + 4] for i in range(0, len(tinted), 4))), 3
            )

    def test_metadata_rejects_names_and_accents_outside_header_limits(self):
        for name, accent in (
            ("x" * 48, 0),
            ("bad\nname", 0),
            ("é" * 24, 0),
            ("Dark", -1),
            ("Dark", 0x1000000),
        ):
            with self.subTest(name=name, accent=accent), self.assertRaises(ValueError):
                compile_theme({}, b"", None, name=name, accent=accent)

    def test_palette_recolor_is_exact_and_preserves_indices_and_header(self):
        for depth in (8, 16):
            original = indexed(depth)
            info = describe(1, 0, original)
            rgba = bytes([10, 20, 30, 50, 40, 50, 60, 70]) * 2
            changed, method = encode_replacement(info, original, 4, 1, rgba)
            self.assertEqual(method, "palette")
            self.assertEqual(changed[:32], original[:32])
            self.assertEqual(changed[40:], original[40:])
            self.assertEqual(changed[32:40], bytes([30, 20, 10, 50, 60, 50, 40, 70]))

    def test_redrawing_can_change_indices_within_existing_budget(self):
        for depth in (8, 16):
            original = indexed(depth)
            info = describe(1, 0, original)
            rgba = bytes([10, 20, 30, 255]) * 2 + bytes([40, 50, 60, 255]) * 2
            changed, method = encode_replacement(info, original, 4, 1, rgba)
            self.assertEqual(method, "indexed pixels")
            expected = (
                bytes([0, 0, 1, 1]) if depth == 8 else struct.pack("<4H", 0, 0, 1, 1)
            )
            self.assertEqual(changed[40:], expected)
            self.assertEqual(len(changed), len(original))

    def test_rejects_color_and_dimension_overflow_without_quantization(self):
        original = indexed()
        info = describe(1, 0, original)
        with self.assertRaisesRegex(ValueError, "3 colors.*budget of 2"):
            encode_replacement(
                info,
                original,
                4,
                1,
                bytes([1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 1, 2, 3, 255]),
            )
        with self.assertRaisesRegex(ValueError, "expected 4x1"):
            encode_replacement(info, original, 2, 2, bytes(16))

    def test_pack_round_trip_and_wrong_input_rejection(self):
        original = indexed()
        catalog = {
            "schema": 1,
            "input": {"bytes": len(original), "sha256": digest(original)},
            "bitmaps": [describe(1, 0, original)],
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / "manifest.json"
            manifest.write_text(
                json.dumps(
                    [{"type": "BMap", "id": "0x1", "name": "Test", "file": "image.png"}]
                )
            )
            rgba = bytes([10, 20, 30, 50, 40, 50, 60, 70]) * 2
            (root / "image.png").write_bytes(png(4, 1, 6, b"\0" + rgba))
            with contextlib.redirect_stdout(io.StringIO()):
                pack = compile_theme(
                    catalog, original, manifest, name="Test Theme", accent=0xB57BE8
                )
            accent, name = METADATA.unpack_from(pack, HEADER.size)
            self.assertEqual(accent, 0xB57BE8)
            self.assertEqual(name.rstrip(b"\0"), b"Test Theme")
            _, _, count, size, crc = HEADER.unpack_from(pack)
            start = HEADER.size + METADATA.size
            self.assertEqual(count, 1)
            self.assertEqual(size, len(pack) - start)
            self.assertEqual(crc, zlib.crc32(pack[start:]))
            index, offset, length = RECORD.unpack_from(pack, start)
            self.assertEqual(index, 0)
            delta = pack[start + RECORD.size : start + RECORD.size + length]
            changed = bytearray(original)
            for _ in range(2):
                for i, value in enumerate(delta):
                    changed[offset + i] ^= value
            self.assertEqual(changed, original)
            with self.assertRaisesRegex(ValueError, "original OSOS"):
                compile_theme(catalog, bytes(len(original)), manifest)
