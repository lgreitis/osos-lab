# SPDX-License-Identifier: GPL-3.0-only
"""Emit runtime resource bounds from the pinned firmware catalog."""

from .theme_format import RECORD, catalog_hash


def generate(catalog, directory):
    bitmaps = catalog["bitmaps"]
    if not bitmaps:
        raise ValueError("Bitmap catalog is empty")
    lines = [
        '#include "assets.h"',
        '#include "osos.h"',
        '#include "theme_package.h"',
        "const struct cfw_bitmap_asset cfw_bitmap_assets[] = {",
    ]
    maximum = 0
    expected = bitmaps[0]["offset"]
    if expected % 4:
        raise ValueError("First catalog bitmap is not word-aligned")
    for a in bitmaps:
        # The firmware packs bitmaps contiguously with 4-byte alignment; the payload
        # derives each address from the previous size instead of storing it.
        if a["offset"] != expected:
            raise ValueError(f"Catalog bitmap {a['id']:#x} is not contiguous")
        expected += (a["bytes"] + 3) & ~3
        lines.append(f"    {{{a['bytes']}u, {a['crc32']}u}},")
        maximum += RECORD.size + ((a["bytes"] - a["data_offset"] + 3) & ~3)
    lines += [
        "};",
        "unsigned char *const cfw_bitmap_assets_base = "
        f"(unsigned char *)({bitmaps[0]['offset']:#x} + OSOS_RUNTIME_BASE - OSOS_LOAD_OFFSET);",
        f"const uint32_t cfw_bitmap_asset_count = {len(bitmaps)};",
        f"_Static_assert({len(bitmaps)} <= CFW_THEME_MAX_ASSETS, "
        '"Theme asset bound is below the catalog");',
        f"const uint32_t cfw_theme_max_bytes = {maximum};",
        "const unsigned char cfw_theme_catalog_hash[32] = {"
        + ",".join(str(b) for b in catalog_hash(catalog))
        + "};",
        "",
    ]
    path = directory / "bitmap_assets.c"
    path.write_text("\n".join(lines))
    return path
