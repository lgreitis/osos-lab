#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render a Settings silhouette SVG with the native bevel and reflection."""

import argparse
import math
import struct
import subprocess
import zlib
from pathlib import Path

WIDTH, HEIGHT, SCALE = 78, 121, 8


def chunk(kind, data):
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data))
    )


def render(source, output):
    w, h = WIDTH * SCALE, HEIGHT * SCALE
    mask = subprocess.check_output(
        [
            "magick",
            "-background",
            "black",
            "-density",
            str(72 * SCALE),
            str(source),
            "-resize",
            f"{w}x{h}!",
            "-alpha",
            "off",
            "-depth",
            "8",
            "gray:-",
        ]
    )
    if len(mask) != w * h:
        raise ValueError("Expected a single SVG image")
    distance = [10000.0 if value >= 128 else 0.0 for value in mask]
    diagonal = math.sqrt(2)
    for step in (1, -1):
        ys = range(1, h - 1) if step == 1 else range(h - 2, 0, -1)
        xs = range(1, w - 1) if step == 1 else range(w - 2, 0, -1)
        for y in ys:
            for x in xs:
                i = y * w + x
                if distance[i]:
                    distance[i] = min(
                        distance[i],
                        distance[i - step] + 1,
                        distance[i - step * w] + 1,
                        distance[i - step * w - 1] + diagonal,
                        distance[i - step * w + 1] + diagonal,
                    )
    shaded = [0.0] * (w * h)
    for y in range(75 * SCALE):
        py = (y + 0.5) / SCALE
        face = 236 - 1.65 * (py - 12)
        rim = 34 + 7 * (py - 10) / 54
        for x in range(w):
            i = y * w + x
            if mask[i]:
                blend = math.sin(
                    min(1, max(0, (distance[i] / SCALE - 0.7) / 1.3)) * math.pi / 2
                )
                shaded[i] = (rim + (face - rim) * blend) * mask[i] / 255
    for y in range(85 * SCALE, h):
        py = (y + 0.5) / SCALE
        source_y = 155 * SCALE - y - 1
        opacity = 0.42 * max(0, (128 - py) / 40) ** 1.5
        for x in range(w):
            shaded[y * w + x] = shaded[source_y * w + x] * opacity
    rows = bytearray()
    for y in range(HEIGHT):
        rows.append(0)
        for x in range(WIDTH):
            rows.append(
                round(
                    sum(
                        shaded[(y * SCALE + sy) * w + x * SCALE + sx]
                        for sy in range(SCALE)
                        for sx in range(SCALE)
                    )
                    / SCALE**2
                )
            )
    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 0, 0, 0, 0)
    output.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(rows, 9))
        + chunk(b"IEND", b"")
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    render(args.input, args.output)


if __name__ == "__main__":
    main()
