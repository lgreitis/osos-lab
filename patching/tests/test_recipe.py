# SPDX-License-Identifier: GPL-3.0-only
"""Shared source references, overlays, and recipe bounds."""

import unittest

from patching.recipe import Input, Recipe, fingerprint


class RecipeTests(unittest.TestCase):
    def test_overlays_keep_apple_bytes_out_of_compiled_data(self):
        recipe = Recipe({"osos": fingerprint(b"native bytes")})
        recipe.overlay(Input("osos", 0, 12), [(7, b"CFW")])
        self.assertEqual(recipe.data, b"CFW")
        self.assertEqual(
            recipe.segments,
            [
                {"kind": "input", "name": "osos", "offset": 0, "bytes": 7},
                {"kind": "data", "offset": 0, "bytes": 3},
                {"kind": "input", "name": "osos", "offset": 10, "bytes": 2},
            ],
        )

    def test_overlay_rejects_conflicts_and_source_bounds(self):
        for spans in (
            [(0, b"a"), (0, b"b")],
            [(-1, b"a")],
            [(4, b"a")],
            [(0, b"")],
            [(0, Input("osos", 3, 2))],
            [(0, Input("bds", 0, 1))],
        ):
            with self.subTest(spans=spans), self.assertRaises(ValueError):
                Recipe({"osos": fingerprint(b"test")}).overlay(bytes(4), spans)

    def test_padding_and_relocations(self):
        recipe = Recipe({"bds": fingerprint(bytes(8))})
        recipe.literal(b"code" + bytes(1024) + b"tail")
        self.assertEqual(recipe.data, b"codetail")
        self.assertEqual(recipe.segments[1], {"kind": "zero", "bytes": 1024})
        for name, base, count in (
            ("other", 0, 1),
            ("bds", -1, 1),
            ("bds", 0xFFFFFFFF, 1),
            ("bds", 0, 0),
        ):
            with (
                self.subTest(name=name, base=base, count=count),
                self.assertRaises(ValueError),
            ):
                recipe.relocate(name, base, count)
        recipe.relocate("bds", 0x1000, 1)
        with self.assertRaises(ValueError):
            recipe.relocate("bds", 0x2000, 1)
