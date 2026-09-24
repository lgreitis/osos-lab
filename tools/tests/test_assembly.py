#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline recipe export checks."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import assembly


class RecipeTests(unittest.TestCase):
    def test_delta_keeps_original_bytes_in_local_inputs(self):
        original = b"Apple original firmware contents"
        result = original[:7] + b"CFW" + original[10:]
        recipe = assembly.Recipe({"osos": original})
        recipe.delta("osos", 0, result)
        self.assertEqual(recipe.output, result)
        self.assertEqual(recipe.data, b"CFW")
        with tempfile.TemporaryDirectory() as directory:
            files = recipe.save(Path(directory), "osos", result)
            metadata = json.loads(Path(files["recipe"]).read_text())
            self.assertEqual(metadata["interface"], assembly.INTERFACE)
            self.assertEqual(metadata["inputs"]["osos"]["bytes"], len(original))

    def test_resource_copies_come_from_local_firmware(self):
        original = b"12345678-Apple template-original data"
        result = b"12345678-CFW template-original data"
        recipe = assembly.Recipe({"osos": original})
        index = {original[i : i + 8]: i for i in range(len(original) - 7)}
        assembly.resource_delta(recipe, result, index)
        self.assertEqual(recipe.output, result)
        self.assertEqual(recipe.data, b"CFW")

    def test_padding_and_invalid_ranges(self):
        recipe = assembly.Recipe({"osos": b"test"})
        recipe.literal(b"code" + bytes(1024) + b"tail")
        self.assertEqual(recipe.data, b"codetail")
        self.assertEqual(recipe.segments[1], {"kind": "zero", "bytes": 1024})
        with self.assertRaises(ValueError):
            recipe.delta("osos", 3, b"too long")


if __name__ == "__main__":
    unittest.main()
