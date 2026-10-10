# SPDX-License-Identifier: GPL-3.0-only
"""PNG filters, alpha and native bitmap packing contracts."""

import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from patching.bitmap import encode_bmap, read_png


def chunk(kind, data):
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data))
    )


def png(width, height, color, rows, palette=b"", alpha=b"", depth=8):
    result = b"\x89PNG\r\n\x1a\n" + chunk(
        b"IHDR", struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, 0)
    )
    if palette:
        result += chunk(b"PLTE", palette)
    if alpha:
        result += chunk(b"tRNS", alpha)
    return result + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


class BitmapTests(unittest.TestCase):
    def read(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.png"
            path.write_bytes(data)
            return read_png(path)

    def test_all_png_filters_decode_known_grayscale_rows(self):
        rows = bytes([0, 10, 20, 30, 1, 11, 10, 10, 2, 1, 1, 1, 3, 7, 5, 5, 4, 1, 1, 1])
        width, height, rgba = self.read(png(3, 5, 0, rows))
        self.assertEqual((width, height), (3, 5))
        values = [10, 20, 30, 11, 21, 31, 12, 22, 32, 13, 22, 32, 14, 23, 33]
        self.assertEqual(rgba, bytes(c for v in values for c in (v, v, v, 255)))

    def test_palette_alpha_and_native_stride_are_preserved(self):
        width, height, rgba = self.read(
            png(
                3, 1, 3, bytes([0, 1, 0, 1]), bytes([1, 2, 3, 4, 5, 6]), bytes([0, 127])
            )
        )
        self.assertEqual(rgba, bytes([4, 5, 6, 127, 1, 2, 3, 0, 4, 5, 6, 127]))
        bitmap = encode_bmap(width, height, rgba)
        self.assertEqual(struct.unpack_from("<HHHH", bitmap), (100, 0, 4, 8))
        self.assertEqual(
            bitmap[28:], bytes([2, 0, 0, 0, 6, 5, 4, 127, 3, 2, 1, 0, 0, 1, 0, 0])
        )

    def test_packed_palettes_preserve_alpha_and_ignore_row_padding(self):
        palette = bytes([10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120])
        alpha = bytes([0, 85, 170, 255])
        for depth, rows, indices in (
            (1, bytes([0, 0x7F, 2, 0x20]), [0, 1, 1, 1, 0, 0]),
            (2, bytes([0, 0x1B, 2, 0xCC]), [0, 1, 2, 3, 2, 1]),
            (4, bytes([1, 0x01, 0x2E, 2, 0x31, 0xF0]), [0, 1, 2, 3, 2, 1]),
        ):
            with self.subTest(depth=depth):
                width, height, rgba = self.read(
                    png(
                        3,
                        2,
                        3,
                        rows,
                        palette[: 3 * (1 << depth)],
                        alpha[: 1 << depth],
                        depth,
                    )
                )
                self.assertEqual((width, height), (3, 2))
                self.assertEqual(
                    rgba,
                    bytes(
                        c
                        for i in indices
                        for c in (*palette[i * 3 : i * 3 + 3], alpha[i])
                    ),
                )

    def test_truecolor_keeps_more_than_256_colors_without_quantization(self):
        rgba = bytes(c for i in range(257) for c in (i & 255, i >> 8, 77, 123))
        width, height, decoded = self.read(png(257, 1, 6, b"\0" + rgba))
        self.assertEqual(decoded, rgba)
        bitmap = encode_bmap(width, height, decoded)
        self.assertEqual(struct.unpack_from("<HHHH", bitmap), (0x1888, 0, 1028, 32))
        self.assertEqual(
            bitmap[28:],
            bytes(c for i in range(257) for c in (77, i >> 8, i & 255, 123)),
        )

    def test_rgb565_texture_layout_and_alpha_constraint(self):
        rgba = bytes([255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255])
        bitmap = encode_bmap(3, 1, rgba, rgb565=True)
        self.assertEqual(struct.unpack_from("<HHHH", bitmap), (0x565, 0, 8, 16))
        self.assertEqual(bitmap[28:], struct.pack("<4H", 0xF800, 0x07E0, 0x001F, 0))
        with self.assertRaisesRegex(ValueError, "opaque PNG"):
            encode_bmap(1, 1, bytes([255, 255, 255, 128]), rgb565=True)

    def test_gles_compatible_encoding_preserves_low_color_pixels_and_alpha(self):
        rgba = bytes([10, 20, 30, 0, 40, 50, 60, 127, 10, 20, 30, 255])
        bitmap = encode_bmap(3, 1, rgba, allow_indexed=False)
        self.assertEqual(struct.unpack_from("<HHHH", bitmap), (0x1888, 0, 12, 32))
        self.assertEqual(bitmap[28:], bytes([30, 20, 10, 0, 60, 50, 40, 127, 30, 20, 10, 255]))
        texture = encode_bmap(1, 1, bytes([255, 0, 0, 255]), rgb565=True, allow_indexed=False)
        self.assertEqual(struct.unpack_from("<HHHH", texture), (0x565, 0, 4, 16))
        self.assertEqual(texture[28:], struct.pack("<2H", 0xf800, 0))
