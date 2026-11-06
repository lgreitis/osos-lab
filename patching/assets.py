# SPDX-License-Identifier: GPL-3.0-only
"""Emit runtime resource bounds from the pinned firmware catalog."""

from .catalog import is_payload
from .theme_format import RECORD, catalog_hash


def generate(catalog, directory):
    bitmaps = catalog["bitmaps"]
    firmware = [a for a in bitmaps if not is_payload(a)]
    if not firmware:
        raise ValueError("Bitmap catalog is empty")
    if bitmaps[: len(firmware)] != firmware:
        raise ValueError("Payload catalog bitmaps must follow the firmware bitmaps")
    lines = [
        '#include "assets.h"',
        '#include "osos.h"',
        '#include "theme_package.h"',
        "extern unsigned char cfw_catalog_bitmaps[];",
        "unsigned char *const cfw_catalog_bitmaps_base = cfw_catalog_bitmaps;",
        "const struct cfw_bitmap_asset cfw_bitmap_assets[] = {",
    ]
    maximum = 0
    expected = {"firmware": firmware[0]["offset"], "payload": 0}
    if expected["firmware"] % 4:
        raise ValueError("First catalog bitmap is not word-aligned")
    for a in bitmaps:
        # Both regions pack bitmaps contiguously with 4-byte alignment; the payload
        # derives each address from the previous size instead of storing it.
        region = "payload" if is_payload(a) else "firmware"
        if a["offset"] != expected[region]:
            raise ValueError(f"Catalog bitmap {a['id']:#x} is not contiguous")
        expected[region] += (a["bytes"] + 3) & ~3
        lines.append(f"    {{{a['bytes']}u, {a['crc32']}u}},")
        maximum += RECORD.size + ((a["bytes"] - a["data_offset"] + 3) & ~3)
    wells = []
    for name in (
        "OptionBar_White_Well_Center_Image",
        "OptionBar_Black_Well_Center_Image",
    ):
        matches = [i for i, a in enumerate(bitmaps) if a["name"] == name]
        if len(matches) != 1:
            raise ValueError(f"Catalog needs exactly one {name}")
        wells.append(matches[0])
    fields = [
        i
        for i, a in enumerate(bitmaps)
        if a["name"] == "System_Input_Field_Middle_Image"
    ]
    if len(fields) != 1:
        raise ValueError("Catalog needs exactly one System_Input_Field_Middle_Image")
    lines += [
        "};",
        f"const uint32_t cfw_bitmap_well_centers[2] = {{{wells[0]}u, {wells[1]}u}};",
        f"const uint32_t cfw_bitmap_field_center = {fields[0]}u;",
        "unsigned char *const cfw_bitmap_assets_base = "
        f"(unsigned char *)({firmware[0]['offset']:#x} + OSOS_RUNTIME_BASE - OSOS_LOAD_OFFSET);",
        f"const uint32_t cfw_bitmap_asset_count = {len(bitmaps)};",
        f"const uint32_t cfw_bitmap_firmware_count = {len(firmware)};",
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
