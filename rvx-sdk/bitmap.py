# SPDX-License-Identifier: GPL-3.0-only
"""PNG to the device's BMap bitmap format, for rvx-pack.

A BMap is a 28-byte header (format, stride in bytes, depth, bounds as top, left,
bottom, right, payload bytes) and its payload; rows are padded to a word.

- Picture, with at most 256 colours: indexed, a 32-bit palette count, the
  palette as B, G, R, A, then one byte per pixel. Otherwise B, G, R, A per
  pixel, not premultiplied.
- Pane picture (`pane = "picture"` in the manifest): a picture of one of the
  sizes the menu panes show (PICTURE_SIZES), or the 20 x 16 badge beside a
  pane's count line (`"badge"`).
- Pane icon (`"icon"`): what the stock Settings pane icons are, a 78 x 121 mask
  of one coverage byte per pixel (0 clear, 255 full) that the device draws in
  its own colour. Coverage is the alpha channel when any pixel is translucent,
  else the gray level of an opaque grayscale PNG.

Ships in the SDK beside rvx-pack, so it depends on nothing outside this file.

Adapted from RepriseOS patching/bitmap.py (bf749b2) by lgreitis.
"""

import struct
import zlib

HEADER = struct.Struct("<HHHHiiiii")
INDEXED8 = 0x64  # BMapFormat_Indexed8
BGRA8888 = 0x1888  # BMapFormat_BGRA8888
MASK8 = 8  # BMapFormat_Mask8, the stock Settings pane icons
ICON_SIZE = (78, 121)
BADGE_SIZE = (20, 16)
# The pictures the menu panes show: a square picture with lines under it, a
# wide one, one with two lines over it, one with a count line and badge under
# it, and one in the World Clock pane's clock box.
PICTURE_SIZES = ((119, 119), (142, 87), (130, 128), (146, 110), (132, 145))
# What an asset's `pane` value may be.
PANES = ("icon", "picture", "badge")


def paeth(a, b, c):
    p = a + b - c
    return min((a, b, c), key=lambda value: abs(p - value))


def read_png(data):
    """Width, height and RGBA bytes of a PNG."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
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
        raise ValueError("export a non-interlaced PNG with 8-bit channels or a palette")
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
            raise ValueError("unknown PNG row filter")
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


def encode_bmap(width, height, rgba, sizes=()):
    """BMap bytes for RGBA pixels: indexed with at most 256 colours, else BGRA;
    `sizes` are the (width, height) the picture may have, any when empty."""
    if sizes and (width, height) not in sizes:
        allowed = ", ".join(f"{w} x {h}" for w, h in sizes)
        raise ValueError(f"the picture is {width} x {height}, not one of {allowed}")
    if len(rgba) != width * height * 4:
        raise ValueError("RGBA pixel length does not match its dimensions")
    pixels = [rgba[i : i + 4] for i in range(0, len(rgba), 4)]
    colors = list(dict.fromkeys(pixels))
    indexed = len(colors) <= 256
    depth = 8 if indexed else 32
    stride = (width * depth // 8 + 3) & ~3
    if not width or not height or stride > 0x7FFF:
        raise ValueError("bitmap dimensions exceed the row stride")
    padding = bytes(stride - width * depth // 8)
    payload = bytearray()
    if indexed:
        payload += struct.pack("<I", len(colors))
        for r, g, b, a in colors:
            payload += bytes((b, g, r, a))
        index = {color: i for i, color in enumerate(colors)}
    for y in range(height):
        for pixel in pixels[y * width : (y + 1) * width]:
            r, g, b, a = pixel
            payload += bytes((index[pixel],) if indexed else (b, g, r, a))
        payload += padding
    fmt = INDEXED8 if indexed else BGRA8888
    return (
        HEADER.pack(fmt, 0, stride, depth, 0, 0, height, width, len(payload)) + payload
    )


def encode_mask(width, height, rgba):
    """Pane icon bytes for RGBA pixels: an ICON_SIZE coverage mask."""
    if (width, height) != ICON_SIZE:
        raise ValueError(
            f"a pane icon is {ICON_SIZE[0]} x {ICON_SIZE[1]}, not {width} x {height}"
        )
    pixels = [rgba[i : i + 4] for i in range(0, len(rgba), 4)]
    if any(a != 255 for _, _, _, a in pixels):
        coverage = [a for _, _, _, a in pixels]
    elif all(r == g == b for r, g, b, _ in pixels):
        coverage = [r for r, _, _, _ in pixels]
    else:
        raise ValueError("a pane icon is coverage: give it alpha, or make it grayscale")
    stride = (width + 3) & ~3
    payload = b"".join(
        bytes(coverage[y * width : (y + 1) * width]) + bytes(stride - width)
        for y in range(height)
    )
    return HEADER.pack(MASK8, 0, stride, 8, 0, 0, height, width, len(payload)) + payload


def png_to_bmap(data, pane=None):
    """The BMap of a PNG; `pane` is what it is for a menu pane, one of PANES,
    None for a bitmap of any size."""
    if pane is not None and pane not in PANES:
        raise ValueError(f"pane is one of {', '.join(PANES)}, not {pane!r}")
    if pane == "icon":
        return encode_mask(*read_png(data))
    sizes = {"picture": PICTURE_SIZES, "badge": (BADGE_SIZE,)}.get(pane, ())
    return encode_bmap(*read_png(data), sizes=sizes)


def bmap_size(bmap):
    """(width, height) of a BMap."""
    _, _, _, _, top, left, bottom, right, _ = HEADER.unpack_from(bmap)
    return right - left, bottom - top
