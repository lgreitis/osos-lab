# SPDX-License-Identifier: GPL-3.0-only
import copy
import tempfile
import unittest
from pathlib import Path

from patching import assets


def entry(offset, size, name="", source=None):
    item = {
        "id": 0x0DAD0000 + offset,
        "name": name,
        "offset": offset,
        "bytes": size,
        "data_offset": 28,
        "crc32": 1,
    }
    if source:
        item["source"] = source
    return item


class AssetsTests(unittest.TestCase):
    def setUp(self):
        self.catalog = {
            "schema": 1,
            "input": {"bytes": 1, "sha256": ""},
            "bitmaps": [
                entry(4096, 30, "OptionBar_White_Well_Center_Image"),
                entry(4128, 32, "OptionBar_Black_Well_Center_Image"),
                entry(4160, 33, "System_Input_Field_Middle_Image"),
                entry(0, 40, "CFW_A", "payload"),
                entry(40, 29, "CFW_B", "payload"),
            ],
        }

    def generate(self, catalog):
        with tempfile.TemporaryDirectory() as directory:
            return Path(assets.generate(catalog, Path(directory))).read_text()

    def test_firmware_and_payload_regions_are_each_contiguous(self):
        source = self.generate(self.catalog)
        self.assertIn("const uint32_t cfw_bitmap_asset_count = 5;", source)
        self.assertIn("const uint32_t cfw_bitmap_firmware_count = 3;", source)
        self.assertIn("(unsigned char *)(0x1000 + OSOS_RUNTIME_BASE", source)
        self.assertIn("cfw_catalog_bitmaps_base = cfw_catalog_bitmaps;", source)
        self.assertEqual(source.count("u, 1u},"), 5)

    def test_rejects_gaps_and_misordered_regions(self):
        for index, offset in ((1, 4132), (4, 44)):
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
