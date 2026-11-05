# SPDX-License-Identifier: GPL-3.0-only
import copy
import unittest
import zlib

from patching.theme_format import HEADER, RECORD, catalog_hash, digest
from themes.style import STYLE_BYTES, classic_style
from themes.stock import compile_baseline


class StockTests(unittest.TestCase):
    def setUp(self):
        self.firmware = bytes(range(100))
        self.catalog = {
            "schema": 1,
            "input": {"bytes": 100, "sha256": digest(self.firmware)},
            "bitmaps": [
                {
                    "offset": 8,
                    "bytes": 41,
                    "data_offset": 32,
                    "crc32": zlib.crc32(self.firmware[8:49]),
                }
            ],
        }

    def test_restores_all_catalog_payloads_with_padding(self):
        pack = compile_baseline(self.catalog, self.firmware)
        body = pack[HEADER.size :]
        self.assertEqual(
            HEADER.unpack_from(pack),
            (b"RTH4", catalog_hash(self.catalog), 0, len(body), 1, zlib.crc32(body)),
        )
        self.assertEqual(body[:STYLE_BYTES], classic_style("light"))
        records = body[STYLE_BYTES:]
        self.assertEqual(RECORD.unpack_from(records), (0, 32, 9))
        self.assertEqual(records[12:], self.firmware[40:49] + bytes(3))

    def test_rejects_wrong_firmware_or_catalog_before_output(self):
        with self.assertRaises(ValueError):
            compile_baseline(self.catalog, self.firmware[:-1])
        for key, value in (
            ("offset", 100),
            ("bytes", 0),
            ("data_offset", 42),
            ("crc32", 0),
        ):
            catalog = copy.deepcopy(self.catalog)
            catalog["bitmaps"][0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                compile_baseline(catalog, self.firmware)
