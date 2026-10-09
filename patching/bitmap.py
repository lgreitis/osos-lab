# SPDX-License-Identifier: GPL-3.0-only
"""PNG decoding and native Apple BMap encoding."""

import struct
import zlib
from pathlib import Path


def paeth(a, b, c):
    p = a + b - c
    return min((a, b, c), key=lambda value: abs(p - value))


def read_png(path):
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Expected a PNG image")
    chunks = {}
    offset = 8
    while offset < len(data):
        size, kind = struct.unpack_from(">I4s", data, offset)
        body = data[offset + 8 : offset + 8 + size]
        crc = struct.unpack_from(">I", data, offset + 8 + size)[0]
        if zlib.crc32(kind + body) != crc:
            raise ValueError("PNG chunk checksum mismatch")
        chunks.setdefault(kind, bytearray()).extend(body)
        offset += size + 12
    width, height, depth, color, compression, filtering, interlace = struct.unpack(
        ">IIBBBBB", chunks[b"IHDR"]
    )
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if (
        not width
        or not height
        or not (depth == 8 or color == 3 and depth in (1, 2, 4))
        or channels is None
        or any((compression, filtering, interlace))
    ):
        raise ValueError("Export a non-interlaced PNG with 8-bit channels or a palette")
    raw = zlib.decompress(chunks[b"IDAT"])
    stride = (width * channels * depth + 7) // 8
    filter_bytes = max(1, (channels * depth + 7) // 8)
    if len(raw) != (stride + 1) * height:
        raise ValueError("PNG pixel length does not match its dimensions")
    palette = chunks.get(b"PLTE", b"")
    alpha = chunks.get(b"tRNS", b"")
    transparent = (
        tuple(struct.unpack(">" + "H" * channels, alpha))
        if alpha and color in (0, 2)
        else None
    )
    previous = bytearray(stride)
    pixels = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        filter_type = raw[start]
        if filter_type > 4:
            raise ValueError("Unknown PNG row filter")
        row = bytearray(raw[start + 1 : start + 1 + stride])
        for x in range(stride):
            a = row[x - filter_bytes] if x >= filter_bytes else 0
            b = previous[x]
            c = previous[x - filter_bytes] if x >= filter_bytes else 0
            predictor = (0, a, b, (a + b) // 2, paeth(a, b, c))[filter_type]
            row[x] = (row[x] + predictor) & 255
        for x in range(width):
            p = tuple(row[x * channels : (x + 1) * channels])
            if color == 3:
                bit = x * depth
                index = (row[bit // 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1)
                if index * 3 + 3 > len(palette):
                    raise ValueError("PNG pixel exceeds palette")
                rgba = (
                    *palette[index * 3 : index * 3 + 3],
                    alpha[index] if index < len(alpha) else 255,
                )
            elif color in (0, 4):
                rgba = (
                    p[0],
                    p[0],
                    p[0],
                    p[1] if color == 4 else 0 if p == transparent else 255,
                )
            else:
                rgba = p if color == 6 else (*p, 0 if p == transparent else 255)
            pixels.extend(rgba)
        previous = row
    return width, height, bytes(pixels)


def encode_bmap(width, height, rgba, *, rgb565=False):
    if len(rgba) != width * height * 4:
        raise ValueError("RGBA pixel length does not match its dimensions")
    colors = list(dict.fromkeys(rgba[i : i + 4] for i in range(0, len(rgba), 4)))
    if rgb565 and any(color[3] != 255 for color in colors):
        raise ValueError("RGB565 textures require an opaque PNG")
    indexed = not rgb565 and len(colors) <= 256
    depth = 16 if rgb565 else 8 if indexed else 32
    stride = (width * (depth // 8) + 3) & ~3
    if not width or not height or stride > 0x7FFF:
        raise ValueError("Bitmap dimensions exceed the native row stride")
    palette = {color: index for index, color in enumerate(colors)} if indexed else {}
    content = bytearray()
    if indexed:
        content.extend(struct.pack("<I", len(colors)))
        for r, g, b, a in colors:
            content.extend((b, g, r, a))
    for y in range(height):
        for x in range(width):
            p = rgba[(y * width + x) * 4 : (y * width + x + 1) * 4]
            if rgb565:
                pixel = ((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3)
                content.extend(struct.pack("<H", pixel))
            else:
                content.extend((palette[p],) if indexed else (p[2], p[1], p[0], p[3]))
        content.extend(bytes(stride - width * (depth // 8)))
    header = struct.pack(
        "<HHHHiiiii",
        0x565 if rgb565 else 0x64 if indexed else 0x1888,
        0,
        stride,
        depth,
        0,
        0,
        height,
        width,
        len(content),
    )
    return header + content
