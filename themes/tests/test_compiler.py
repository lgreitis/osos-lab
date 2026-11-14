# SPDX-License-Identifier: GPL-3.0-only
"""Native theme limits, native pixel encoding and pack contracts."""

import contextlib
import io
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from patching.tests.test_bitmap import png
from patching.theme_format import HEADER, MAGIC, RECORD, catalog_hash, digest
from themes.compiler import (
    compile_theme,
    describe,
    encode_pixels,
    fit_palette,
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
    def test_indexed_pixels_preserve_color_alpha_and_native_storage(self):
        for depth in (8, 16):
            original = indexed(depth)
            info = describe(1, 0, original)
            rgba = bytes([10, 20, 30, 50, 40, 50, 60, 70]) * 2
            changed = encode_pixels(info, 4, 1, rgba)
            self.assertEqual(changed[8:], original[40:])
            self.assertEqual(changed[:8], bytes([30, 20, 10, 50, 60, 50, 40, 70]))

    def test_redrawing_can_change_indices_within_existing_budget(self):
        for depth in (8, 16):
            original = indexed(depth)
            info = describe(1, 0, original)
            rgba = bytes([10, 20, 30, 255]) * 2 + bytes([40, 50, 60, 255]) * 2
            changed = encode_pixels(info, 4, 1, rgba)
            expected = (
                bytes([0, 0, 1, 1]) if depth == 8 else struct.pack("<4H", 0, 0, 1, 1)
            )
            self.assertEqual(changed[8:], expected)
            self.assertEqual(len(changed), len(original) - 32)

    def test_rejects_color_and_dimension_overflow_without_quantization(self):
        original = indexed()
        info = describe(1, 0, original)
        with self.assertRaisesRegex(ValueError, "3 colors.*budget of 2"):
            encode_pixels(
                info,
                4,
                1,
                bytes([1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 1, 2, 3, 255]),
            )
        with self.assertRaisesRegex(ValueError, "expected 4x1"):
            encode_pixels(info, 2, 2, bytes(16))

    def test_pack_contains_native_pixels_without_firmware_input(self):
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
                pack = compile_theme(catalog, manifest)
            magic, fingerprint, count, size, crc = HEADER.unpack_from(pack)
            self.assertEqual(
                (magic, fingerprint, count), (MAGIC, catalog_hash(catalog), 1)
            )
            self.assertEqual(size, len(pack) - HEADER.size)
            self.assertEqual(crc, zlib.crc32(pack[HEADER.size :]))
            index, offset, length = RECORD.unpack_from(pack, HEADER.size)
            self.assertEqual((index, offset, length), (0, 32, len(original) - 32))
            pixels = pack[
                HEADER.size + RECORD.size : HEADER.size + RECORD.size + length
            ]
            self.assertEqual(
                pixels, bytes([30, 20, 10, 50, 60, 50, 40, 70, 0, 1, 0, 1])
            )

            rgba = bytes([1, 2, 3, 255, 4, 5, 6, 127, 7, 8, 9, 0, 1, 2, 3, 255])
            (root / "image.png").write_bytes(png(4, 1, 6, b"\0" + rgba))
            with contextlib.redirect_stdout(io.StringIO()):
                pack = compile_theme(catalog, manifest)
            index, offset, length = RECORD.unpack_from(pack, HEADER.size)
            self.assertEqual(index, 0)
            self.assertGreaterEqual(offset, catalog["bitmaps"][0]["data_offset"])
            pixels = pack[
                HEADER.size + RECORD.size : HEADER.size + RECORD.size + length
            ]
            self.assertTrue(all(index < 2 for index in pixels[8:]))

    def test_palette_reduction_preserves_transparency_and_opaque_art(self):
        rgba = bytes([0, 0, 0, 0, 0, 161, 255, 64, 0, 161, 255, 128, 0, 161, 255, 255])
        reduced = fit_palette(rgba, 3)
        self.assertEqual(reduced[:4], bytes(4))
        self.assertLessEqual(
            len(set(reduced[i : i + 4] for i in range(0, len(reduced), 4))), 3
        )
        self.assertEqual(fit_palette(reduced, 3), reduced)
        opaque = bytes([0, 0, 0, 255]) * 10 + bytes([0, 161, 255, 255, 0, 80, 127, 255])
        reduced = fit_palette(opaque, 2)
        self.assertEqual(reduced[:4], bytes([0, 0, 0, 255]))
        self.assertEqual(reduced[3::4], opaque[3::4])
