# SPDX-License-Identifier: GPL-3.0-only
"""Compile artwork into reversible disk themes within native bitmap budgets."""

import json
import struct
import zlib

from patching.bitmap import read_png
from patching.theme_format import HEADER, MAGIC, METADATA, RECORD, catalog_hash, digest


def describe(resource_id, offset, data, name=""):
    fmt, _, stride, depth = struct.unpack_from("<4H", data)
    height, width = struct.unpack_from("<II", data, 16)
    start = 28 if struct.unpack_from("<I", data, 24)[0] == len(data) - 28 else 32
    if struct.unpack_from("<I", data, start - 4)[0] != len(data) - start:
        raise ValueError(f"{resource_id:#x}: invalid bitmap length")
    bits = {4: 4, 8: 8, 0x64: 8, 0x65: 16, 0x565: 16, 0x1888: 32}
    if fmt not in bits or depth != bits[fmt] or not width or not height:
        raise ValueError(f"{resource_id:#x}: unsupported native bitmap format")
    colors = struct.unpack_from("<I", data, start)[0] if fmt in (0x64, 0x65) else 0
    pixels = start + 4 + colors * 4 if colors else start
    if (colors and colors > 1 << depth) or stride < (width * depth + 7) // 8:
        raise ValueError(f"{resource_id:#x}: invalid palette or stride")
    if pixels + stride * height != len(data):
        raise ValueError(f"{resource_id:#x}: invalid pixel storage")
    return {
        "id": resource_id,
        "name": name,
        "offset": offset,
        "bytes": len(data),
        "format": fmt,
        "depth": depth,
        "width": width,
        "height": height,
        "stride": stride,
        "data_offset": start + (4 if colors else 0),
        "pixels_offset": pixels,
        "palette_colors": colors,
        "crc32": zlib.crc32(data),
    }


def encode_replacement(info, original, width, height, rgba):
    if (width, height) != (info["width"], info["height"]):
        raise ValueError(
            f"expected {info['width']}x{info['height']}, got {width}x{height}"
        )
    colors = [rgba[i : i + 4] for i in range(0, len(rgba), 4)]
    if len(colors) != width * height:
        raise ValueError("invalid PNG pixel count")
    result = bytearray(original)
    fmt, stride, start = info["format"], info["stride"], info["pixels_offset"]
    capacity = info["palette_colors"]
    if capacity:
        unique = list(dict.fromkeys(colors))
        if len(unique) > capacity:
            raise ValueError(
                f"{len(unique)} colors exceed the native palette budget of {capacity}"
            )
        mapping = {}
        for y in range(height):
            for x in range(width):
                at = start + y * stride + x * (info["depth"] // 8)
                index = (
                    original[at]
                    if fmt == 0x64
                    else struct.unpack_from("<H", original, at)[0]
                )
                if index >= capacity:
                    raise ValueError("stock pixel exceeds palette")
                mapping.setdefault(index, set()).add(colors[y * width + x])
        palette_only = all(len(values) == 1 for values in mapping.values())
        if palette_only:
            palette = {index: next(iter(values)) for index, values in mapping.items()}
        else:
            palette = dict(enumerate(unique))
            lookup = {color: index for index, color in palette.items()}
            for y in range(height):
                for x in range(width):
                    at = start + y * stride + x * (info["depth"] // 8)
                    index = lookup[colors[y * width + x]]
                    if fmt == 0x64:
                        result[at] = index
                    else:
                        struct.pack_into("<H", result, at, index)
        for index, (red, green, blue, alpha) in palette.items():
            at = info["data_offset"] + index * 4
            result[at : at + 4] = bytes((blue, green, red, alpha))
        return bytes(result), "palette" if palette_only else "indexed pixels"
    for y in range(height):
        for x in range(width):
            red, green, blue, alpha = colors[y * width + x]
            at = start + y * stride + x * (info["depth"] // 8)
            if fmt == 0x1888:
                result[at : at + 4] = bytes((blue, green, red, alpha))
            elif fmt == 0x565:
                if alpha != 255:
                    raise ValueError("native RGB565 requires opaque pixels")
                struct.pack_into(
                    "<H", result, at, (red >> 3) << 11 | (green >> 2) << 5 | blue >> 3
                )
            else:
                step = 17 if fmt == 4 else 1
                if red != green or green != blue or alpha != 255 or red % step:
                    raise ValueError(
                        f"native grayscale requires opaque gray levels in steps of {step}"
                    )
                if fmt == 8:
                    result[at] = red
                else:
                    at = start + y * stride + x // 2
                    shift = 4 if x % 2 == 0 else 0
                    result[at] = (result[at] & ~(15 << shift)) | (
                        (red // step) << shift
                    )
    return bytes(result), "native pixels"


def source_hash(manifest):
    data = bytearray(manifest.read_bytes())
    for entry in json.loads(manifest.read_text()):
        data.extend((manifest.parent / entry["file"]).read_bytes())
    return digest(data)


def compile_theme(catalog, firmware, manifest, *, name="", accent=0):
    encoded_name = name.encode("utf-8")
    if len(encoded_name) >= 48 or any(c < 32 for c in encoded_name):
        raise ValueError(
            "Theme names must fit in 47 UTF-8 bytes without control characters"
        )
    if not 0 <= accent <= 0xFFFFFF:
        raise ValueError("Theme accent must be an RGB color")
    if {"bytes": len(firmware), "sha256": digest(firmware)} != catalog["input"]:
        raise ValueError("Expected the catalog's original OSOS image")
    assets = catalog["bitmaps"]
    by_id = {asset["id"]: i for i, asset in enumerate(assets)}
    selected = {}
    for entry in json.loads(manifest.read_text()):
        resource_id = int(entry["id"], 0)
        if (
            entry["type"] != "BMap"
            or resource_id not in by_id
            or resource_id in selected
        ):
            raise ValueError(f"Invalid or duplicate bitmap: {entry['id']}")
        selected[resource_id] = entry
    body = bytearray()
    count = 0
    for resource_id, entry in sorted(selected.items()):
        index = by_id[resource_id]
        info = assets[index]
        original = firmware[info["offset"] : info["offset"] + info["bytes"]]
        if describe(resource_id, info["offset"], original, info["name"]) != info:
            raise ValueError("Bitmap metadata differs from the original firmware")
        try:
            width, height, rgba = read_png(manifest.parent / entry["file"])
            if accent and entry["name"] in (
                "NowPlaying_White_Play_Image",
                "NowPlaying_White_Pause_Image",
            ):
                rgba = tint_icon(rgba, accent)
            replacement, method = encode_replacement(
                info, original, width, height, rgba
            )
        except ValueError as error:
            raise ValueError(f"{entry['name']} ({entry['file']}): {error}") from error
        delta = bytes(a ^ b for a, b in zip(original, replacement, strict=True))
        first = next((i for i, value in enumerate(delta) if value), len(delta))
        end = len(delta.rstrip(b"\0"))
        if first < end:
            assert first >= info["data_offset"]
            body.extend(RECORD.pack(index, first, end - first))
            body.extend(delta[first:end])
            body.extend(bytes(-len(body) % 4))
            count += 1
        print(f"{entry['name']}: {method}, {max(0, end - first):,} delta bytes")
    if not count:
        raise ValueError("Theme does not change any stock artwork")
    return (
        HEADER.pack(MAGIC, catalog_hash(catalog), count, len(body), zlib.crc32(body))
        + METADATA.pack(accent, encoded_name)
        + body
    )


def tint_icon(rgba, accent):
    channels = [(accent >> shift) & 255 for shift in (16, 8, 0)]
    result = bytearray(rgba)
    for at in range(0, len(result), 4):
        light = max(result[at : at + 3])
        for channel in range(3):
            result[at + channel] = (channels[channel] * light + 127) // 255
    return bytes(result)
