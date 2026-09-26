# SPDX-License-Identifier: GPL-3.0-only
"""Native bytes stay source references through cloning and UI edits."""

import struct
import unittest

from patching.native_ui import blocks, pack_blocks, put, word
from patching.resource import Resource, Template


class ResourceTests(unittest.TestCase):
    def test_cloning_and_replacement_preserve_only_untouched_source_ranges(self):
        template = Template(0x100, 24, {0: 1, 4: 12, 8: 42, 12: 10})
        resource = Resource.template(template)
        items = blocks(resource)
        self.assertEqual(word(items[0][1], 0), 10)
        put(items[0][1], 4, 99)
        changed = pack_blocks(items)
        self.assertEqual(list(changed.copies()), [(12, 0x10C, 4), (20, 0x114, 4)])
        self.assertEqual(word(changed, 16), 99)
        self.assertEqual(list(resource.copies()), [(0, 0x100, 24)])
        self.assertEqual(bytes(changed.data[20:]), bytes(4))

    def test_missing_structure_fails_instead_of_using_placeholder_zero(self):
        resource = Resource.template(Template(0x100, 16, {}))
        with self.assertRaisesRegex(
            ValueError, "Missing UI structure metadata at 0x104"
        ):
            word(resource, 4)
        with self.assertRaises(ValueError):
            resource[0:4] = b"short"
        put(resource, 0, 17)
        self.assertEqual(word(resource, 0), 17)
        with self.assertRaises(ValueError):
            word(resource, 2)

    def test_extraction_records_only_accessed_structural_words(self):
        template = Template(4, 8, {}, b"head" + struct.pack("<I", 7) + b"body")
        resource = Resource.template(template)
        self.assertEqual(word(resource, 0), 7)
        self.assertEqual(template.words, {0: 7})
        self.assertEqual(bytes(resource.data), bytes(8))
