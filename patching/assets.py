# SPDX-License-Identifier: GPL-3.0-only
"""Emit runtime resource bounds from the pinned firmware catalog."""

from .catalog import is_payload
from .theme_format import RECORD, catalog_hash

# Stock blue artwork that follows the accent while a theme leaves it stock, with
# the color role each takes. Blue that marks content (capacity legends, playlist
# kinds) is not listed.
ACCENT_ART = (
    (
        "CFW_THEME_SELECTION",
        (
            "OptionBar_White_Thumb_LeftCap_Image",
            "OptionBar_White_Thumb_Center_Image",
            "OptionBar_White_Thumb_RightCap_Image",
            "OptionBar_Black_Thumb_LeftCap_Image",
            "OptionBar_Black_Thumb_Center_Image",
            "OptionBar_Black_Thumb_RightCap_Image",
            "System_ActiveButtonLeftCap_Image",
            "System_ActiveButtonRightCap_Image",
            "System_ActiveButtonFill_Image",
            "DateTimePicker_Selection",
            "Alarms_Timer_Time_Panel_Focus",
            "Settings_DateTime_Capsule_Hilite",
            "Calendar_Hilite_Cell",
            "LockScreen_TumblerFocus_Image",
            "Settings_MainMenu_CheckmarkWhite_Image",
        ),
    ),
    (
        "CFW_THEME_ACCENT_FILL",
        (
            "NowPlaying_ProgressBar_Active_Fill_Image",
            "NowPlaying_ProgressBar_Scrub_Image",
            "Genius_Slider_Endcap_Image",
            "Genius_Slider_Fill_Image",
            "Podcasts_new_item_Image",
            "Podcasts_new_item_video_Image",
            "NowPlaying_Blue_Dot_Image",
            "NowPlaying_Blue_Star_Image",
            "NowPlaying_Large_Blue_Star_Image",
            "StatusBarWhite_PlayStatus_Image",
            "NowPlaying_White_Play_Image",
            "NowPlaying_White_Pause_Image",
            "NowPlaying_White_FF_Image",
            "NowPlaying_White_Rew_Image",
            "Radio_Preset_Image",
            "Radio_Signal_Indicator_Light_Image",
        ),
    ),
)
RECOLORABLE_FORMATS = (0x64, 0x65, 0x1888)


def unique_index(bitmaps, name):
    matches = [i for i, a in enumerate(bitmaps) if a["name"] == name]
    if len(matches) != 1:
        raise ValueError(f"Catalog needs exactly one {name}")
    return matches[0]


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
        "const struct cfw_bitmap_asset cfw_bitmap_assets[] = {",
    ]
    maximum = 0
    expected = {"firmware": firmware[0]["offset"], "payload": 0}
    if expected["firmware"] % 4:
        raise ValueError("First catalog bitmap is not word-aligned")
    for a in bitmaps:
        # Both regions list bitmaps contiguously with 4-byte alignment; the firmware
        # derives each address from the previous size instead of storing it.
        region = "payload" if is_payload(a) else "firmware"
        if a["offset"] != expected[region]:
            raise ValueError(f"Catalog bitmap {a['id']:#x} is not contiguous")
        expected[region] += (a["bytes"] + 3) & ~3
        lines.append(f"    {{{a['bytes']}u, {a['crc32']}u}},")
        maximum += RECORD.size + ((a["bytes"] - a["data_offset"] + 3) & ~3)
    wells = [
        unique_index(bitmaps, name)
        for name in (
            "OptionBar_White_Well_Center_Image",
            "OptionBar_Black_Well_Center_Image",
        )
    ]
    field = unique_index(bitmaps, "System_Input_Field_Middle_Image")
    accent_art = []
    for role, names in ACCENT_ART:
        for name in names:
            index = unique_index(bitmaps, name)
            if bitmaps[index]["format"] not in RECOLORABLE_FORMATS:
                raise ValueError(f"{name} is not paletted or ARGB")
            accent_art.append(f"    {{{index}u, {role}}},")
    lines += [
        "};",
        f"const uint32_t cfw_bitmap_well_centers[2] = {{{wells[0]}u, {wells[1]}u}};",
        f"const uint32_t cfw_bitmap_field_center = {field}u;",
        "const struct cfw_accent_art cfw_bitmap_accent_art[] = {",
        *accent_art,
        "};",
        f"const uint32_t cfw_bitmap_accent_art_count = {len(accent_art)};",
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
