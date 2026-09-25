#!/usr/bin/env python3
"""Encode a 55x55 RGB/RGBA PNG as native little-endian RGB565 artwork."""

import argparse
import struct
import zlib
from pathlib import Path


def paeth(a, b, c):
    p = a + b - c
    return min((a, b, c), key=lambda value: abs(p - value))


def encode(source):
    png = source.read_bytes()
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Expected PNG artwork")

    offset, channels = 8, 0
    compressed = bytearray()
    while offset < len(png):
        length, kind = struct.unpack_from(">I4s", png, offset)
        data = png[offset + 8 : offset + 8 + length]
        checksum = struct.unpack_from(">I", png, offset + 8 + length)[0]
        if zlib.crc32(kind + data) != checksum:
            raise ValueError("PNG checksum mismatch")
        offset += length + 12

        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = (
                struct.unpack(">IIBBBBB", data)
            )
            format_fields = (width, height, depth, compression, filtering, interlace)
            if format_fields != (55, 55, 8, 0, 0, 0) or color not in (2, 6):
                raise ValueError(
                    "Artwork must be a non-interlaced 55x55 8-bit RGB/RGBA PNG"
                )
            channels = 4 if color == 6 else 3
        elif kind == b"IDAT":
            compressed.extend(data)
        elif kind == b"IEND":
            break

    if not channels:
        raise ValueError("Missing PNG header")

    pixels = zlib.decompress(compressed)
    stride = 55 * channels
    if len(pixels) != 55 * (stride + 1):
        raise ValueError("Unexpected PNG pixel length")

    result = bytearray(struct.pack("<III4s", 55, 55, 112, b"565L"))
    previous = bytearray(stride)
    for y in range(55):
        start = y * (stride + 1)
        filter_type = pixels[start]
        if filter_type > 4:
            raise ValueError("Unsupported PNG row filter")

        row = bytearray(pixels[start + 1 : start + 1 + stride])
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            above = previous[i]
            corner = previous[i - channels] if i >= channels else 0
            predictors = (
                0,
                left,
                above,
                (left + above) // 2,
                paeth(left, above, corner),
            )
            row[i] = (row[i] + predictors[filter_type]) & 255

        for x in range(55):
            p = x * channels
            alpha = row[p + 3] if channels == 4 else 255
            # The native format is opaque; composite transparent pixels over black.
            r, g, b = ((value * alpha + 127) // 255 for value in row[p : p + 3])
            rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            result.extend(struct.pack("<H", rgb565))

        result.extend(b"\0\0")
        previous = row

    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    data = encode(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f"Artwork: {args.output} ({len(data)} bytes)")
