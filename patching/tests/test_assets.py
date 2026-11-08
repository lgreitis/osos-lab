# SPDX-License-Identifier: GPL-3.0-only
import copy
import tempfile
import unittest
from pathlib import Path

from patching import assets


def entry(offset, size, name="", source=None, fmt=0x1888):
    item = {
        "id": 0x0DAD0000 + offset,
        "name": name,
        "offset": offset,
        "bytes": size,
        "format": fmt,
        "data_offset": 28,
        "crc32": 1,
    }
    if source:
        item["source"] = source
    return item


ACCENT_NAMES = [name for _, names in assets.ACCENT_ART for name in names]
FIRMWARE = 10 + len(ACCENT_NAMES)


class AssetsTests(unittest.TestCase):
    def setUp(self):
        self.catalog = {
            "schema": 1,
            "input": {"bytes": 1, "sha256": ""},
            "bitmaps": [
                entry(4096, 30, "OptionBar_White_Well_Center_Image"),
                entry(4128, 32, "OptionBar_Black_Well_Center_Image"),
                entry(4160, 33, "System_Input_Field_Middle_Image"),
                entry(4196, 4, "OptionBar_NowPlaying_Well_Center_Image"),
                *(
                    entry(4200 + 4 * i, 4, name)
                    for i, name in enumerate(
                        (
                            "NowPlaying_StatusBar_Image",
                            "LockScreen_TumblerBgnd_Image",
                            "DateTimePicker_Well",
                            "CoverFlow_Backside_Top_Image",
                            "Calendar_Active_Cell",
                            "Calendar_Inactive_Cell",
                        )
                    )
                ),
                *(entry(4224 + 4 * i, 4, name) for i, name in enumerate(ACCENT_NAMES)),
                entry(0, 40, "CFW_A", "payload", 8),
                entry(40, 29, "CFW_B", "payload", 8),
            ],
        }

    def generate(self, catalog):
        with tempfile.TemporaryDirectory() as directory:
            return Path(assets.generate(catalog, Path(directory))).read_text()

    def test_firmware_and_payload_regions_are_each_contiguous(self):
        source = self.generate(self.catalog)
        self.assertIn(
            f"const uint32_t cfw_bitmap_asset_count = {FIRMWARE + 2};", source
        )
        self.assertIn(f"const uint32_t cfw_bitmap_firmware_count = {FIRMWARE};", source)
        self.assertIn("(unsigned char *)(0x1000 + OSOS_RUNTIME_BASE", source)
        self.assertNotIn("cfw_catalog", source)
        self.assertIn(
            "const uint32_t cfw_bitmap_well_centers[3] = {0u, 1u, 3u};", source
        )
        self.assertIn("    {0xdad020eu, 1u, 4u},", source)
        self.assertIn("    {0xdad0688u, 4u, 5u},", source)
        self.assertIn("const uint32_t cfw_bitmap_contrast_text_count = 5;", source)
        hilite = 10 + ACCENT_NAMES.index("Calendar_Hilite_Cell")
        self.assertIn(
            f"const uint32_t cfw_bitmap_calendar_cells[3] = {{8u, 9u, {hilite}u}};",
            source,
        )
        self.assertEqual(source.count("u, 1u},"), FIRMWARE + 2)

    def test_accent_art_lists_each_asset_with_its_role(self):
        source = self.generate(self.catalog)
        count = len(ACCENT_NAMES)
        self.assertIn(f"const uint32_t cfw_bitmap_accent_art_count = {count};", source)
        self.assertIn("    {10u, CFW_THEME_SELECTION},", source)
        self.assertIn(f"    {{{FIRMWARE - 1}u, CFW_THEME_ACCENT_FILL}},", source)

    def test_accent_art_needs_one_recolorable_asset_per_name(self):
        for change in ("missing", "duplicate", "mask"):
            catalog = copy.deepcopy(self.catalog)
            bitmap = catalog["bitmaps"][10]
            if change == "missing":
                bitmap["name"] = "Other"
            elif change == "duplicate":
                catalog["bitmaps"][11]["name"] = bitmap["name"]
            else:
                bitmap["format"] = 8
            message = "not paletted or ARGB" if change == "mask" else "exactly one"
            with (
                self.subTest(change=change),
                self.assertRaisesRegex(ValueError, message),
            ):
                self.generate(catalog)

    def test_rejects_gaps_and_misordered_regions(self):
        for index, offset in ((1, 4132), (FIRMWARE + 1, 44)):
            catalog = copy.deepcopy(self.catalog)
            catalog["bitmaps"][index]["offset"] = offset
            with (
                self.subTest(index=index),
                self.assertRaisesRegex(ValueError, "not contiguous"),
            ):
                self.generate(catalog)
        catalog = copy.deepcopy(self.catalog)
        catalog["bitmaps"].insert(0, catalog["bitmaps"].pop())
        with self.assertRaisesRegex(ValueError, "follow the firmware"):
            self.generate(catalog)
