# SPDX-License-Identifier: GPL-3.0-only
"""Compile artwork into disk themes within native bitmap budgets."""

import json
import struct
import zlib
from collections import Counter

from patching.bitmap import read_png
from patching.theme_format import HEADER, MAGIC, RECORD, catalog_hash


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


def encode_pixels(info, width, height, rgba):
    if (width, height) != (info["width"], info["height"]):
        raise ValueError(
            f"expected {info['width']}x{info['height']}, got {width}x{height}"
        )
    colors = [rgba[i : i + 4] for i in range(0, len(rgba), 4)]
    if len(colors) != width * height:
        raise ValueError("invalid PNG pixel count")
    result = bytearray(info["bytes"] - info["data_offset"])
    fmt, stride = info["format"], info["stride"]
    start = info["pixels_offset"] - info["data_offset"]
    capacity = info["palette_colors"]
    if capacity:
        unique = list(dict.fromkeys(colors))
        if len(unique) > capacity:
            raise ValueError(
                f"{len(unique)} colors exceed the native palette budget of {capacity}"
            )
        lookup = {color: index for index, color in enumerate(unique)}
        for index, (red, green, blue, alpha) in enumerate(unique):
            result[index * 4 : index * 4 + 4] = bytes((blue, green, red, alpha))
        for y in range(height):
            for x in range(width):
                at = start + y * stride + x * (info["depth"] // 8)
                index = lookup[colors[y * width + x]]
                if fmt == 0x64:
                    result[at] = index
                else:
                    struct.pack_into("<H", result, at, index)
        return bytes(result)
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
    return bytes(result)


def premultiplied(color):
    return tuple(value * color[3] / 255 for value in color[:3]) + (color[3],)


def fit_palette(rgba, capacity):
    """Weighted median cut in premultiplied RGBA, reserving transparent pixels."""
    pixels = [tuple(rgba[i : i + 4]) for i in range(0, len(rgba), 4)]
    counts = Counter(pixels)
    if len(counts) <= capacity:
        return rgba
    transparent = any(color[3] == 0 for color in counts)
    palette = [(0, 0, 0, 0)] if transparent and capacity > 1 else []
    colors = [color for color in counts if color[3] or not palette]
    points = {color: premultiplied(color) for color in colors}
    boxes = [colors] if colors else []
    while len(boxes) + len(palette) < capacity:
        candidates = []
        for i, box in enumerate(boxes):
            if len(box) < 2:
                continue
            spans = [
                max(points[c][axis] for c in box) - min(points[c][axis] for c in box)
                for axis in range(4)
            ]
            axis = max(range(4), key=spans.__getitem__)
            candidates.append((spans[axis] ** 2 * sum(counts[c] for c in box), i, axis))
        if not candidates:
            break
        _, index, axis = max(candidates)
        box = sorted(boxes.pop(index), key=lambda color: (points[color][axis], color))
        halfway = sum(counts[c] for c in box) / 2
        accumulated = 0
        split = 1
        for split, color in enumerate(box[:-1], 1):
            accumulated += counts[color]
            if accumulated >= halfway:
                break
        boxes.extend((box[:split], box[split:]))
    for box in boxes:
        total = sum(counts[c] for c in box)
        palette.append(
            tuple(
                round(sum(c[axis] * counts[c] for c in box) / total)
                for axis in range(4)
            )
        )
    palette_points = [premultiplied(color) for color in palette]
    mapping = {}
    for color in counts:
        point = premultiplied(color)
        index = min(
            range(len(palette)),
            key=lambda i: sum((a - b) ** 2 for a, b in zip(point, palette_points[i])),
        )
        mapping[color] = bytes(palette[index])
    return b"".join(mapping[color] for color in pixels)


def compile_theme(catalog, manifest):
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
        try:
            width, height, rgba = read_png(manifest.parent / entry["file"])
            capacity = info["palette_colors"]
            fitted = fit_palette(rgba, capacity) if capacity else rgba
            payload = encode_pixels(info, width, height, fitted)
            method = (
                f"reduced to {capacity} colors" if fitted != rgba else "native pixels"
            )
        except ValueError as error:
            raise ValueError(f"{entry['name']} ({entry['file']}): {error}") from error
        body.extend(RECORD.pack(index, info["data_offset"], len(payload)))
        body.extend(payload)
        body.extend(bytes(-len(body) % 4))
        count += 1
        print(f"{entry['name']}: {method}, {len(payload):,} bytes")
    if not count:
        raise ValueError("Theme manifest is empty")
    return (
        HEADER.pack(MAGIC, catalog_hash(catalog), count, len(body), zlib.crc32(body))
        + body
    )
