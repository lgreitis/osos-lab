# SPDX-License-Identifier: GPL-3.0-only
"""Compile manifest-mapped PNGs into dark-mode bitmap replacements."""

import json

from .bitmap import encode_bmap, read_png
from .ui.resources import word


def native_assets(resources, manifest):
    entries, seen = [], set()
    for entry in json.loads(manifest.read_text()):
        resource_id = int(entry["id"], 0)
        key = entry["type"], resource_id
        if key[0] != "BMap" or key in seen:
            raise ValueError("Assets require unique BMap resource IDs")
        seen.add(key)
        original = resources.stock(*key)
        entries.append(
            (
                entry,
                word(original, 20),
                word(original, 16),
                (word(original, 0) & 0xFFFF) == 0x565,
            )
        )
    return entries


def generate(resources, manifest, directory):
    lines = ['#include "assets.h"', ""]
    rows, total = [], 0
    for index, (entry, native_width, native_height, rgb565) in enumerate(
        native_assets(resources, manifest)
    ):
        resource_id = int(entry["id"], 0)
        width, height, pixels = read_png(manifest.parent / entry["file"])
        expected = native_width, native_height
        if (width, height) != expected:
            raise ValueError(
                f"{entry['file']}: expected {expected}, got {(width, height)}"
            )
        # Apple's GLES path rejects indexed BMap format 0x64; preserve native RGB565.
        bitmap = encode_bmap(width, height, pixels, rgb565=rgb565, allow_indexed=False)
        symbol = f"cfw_asset_{index}"
        lines.append(
            f"static const unsigned char {symbol}[] __attribute__((aligned(4))) = {{"
        )
        for offset in range(0, len(bitmap), 16):
            lines.append(
                "    "
                + ", ".join(f"0x{b:02x}" for b in bitmap[offset : offset + 16])
                + ","
            )
        lines.append("};")
        rows.append(f"    {{{resource_id:#x}, {symbol}, 0}},")
        total += len(bitmap)
        print(
            f"Asset {entry['name']} ({resource_id:#010x}): {width}x{height}, {len(bitmap):,} bytes ({entry['file']})",
            flush=True,
        )
    lines += [
        "struct cfw_bitmap_asset cfw_bitmap_assets[] = {",
        *rows,
        "};",
        f"const uint32_t cfw_bitmap_asset_count = {len(rows)};",
        "",
    ]
    directory.mkdir(parents=True, exist_ok=True)
    generated = directory / "bitmap_assets.c"
    generated.write_text("\n".join(lines))
    print(f"Embedded bitmap data: {total:,} bytes", flush=True)
    return generated
