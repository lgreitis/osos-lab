# SPDX-License-Identifier: GPL-3.0-only
"""Emit runtime resource bounds from the pinned firmware catalog."""

from .theme_format import RECORD, catalog_hash


def generate(catalog, directory):
    lines = [
        '#include "assets.h"',
        '#include "osos.h"',
        "const struct cfw_bitmap_asset cfw_bitmap_assets[] = {",
    ]
    maximum = 0
    for a in catalog["bitmaps"]:
        values = [
            a[k]
            for k in (
                "bytes",
                "data_offset",
                "pixels_offset",
                "palette_colors",
                "stride",
                "width",
                "height",
                "depth",
                "crc32",
            )
        ]
        lines.append(
            f"    {{(unsigned char *)({a['offset']:#x} + OSOS_RUNTIME_BASE - OSOS_LOAD_OFFSET), "
            + ", ".join(str(v) + "u" for v in values)
            + "},"
        )
        maximum += RECORD.size + ((a["bytes"] - a["data_offset"] + 3) & ~3)
    lines += [
        "};",
        f"const uint32_t cfw_bitmap_asset_count = {len(catalog['bitmaps'])};",
        f"const uint32_t cfw_theme_max_bytes = {maximum};",
        "const unsigned char cfw_theme_catalog_hash[32] = {"
        + ",".join(str(b) for b in catalog_hash(catalog))
        + "};",
        "",
    ]
    path = directory / "bitmap_assets.c"
    path.write_text("\n".join(lines))
    return path
