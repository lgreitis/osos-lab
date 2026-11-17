# SPDX-License-Identifier: GPL-3.0-only
"""Native bytes stay source references through cloning and UI edits."""

import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from patching.ui.codegen import emit_resources, resource_symbols
from patching.ui.resources import (
    Resource,
    Resources,
    Template,
    blocks,
    pack_blocks,
    put,
    word,
)


class ResourceTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "Host C compiler is required")
    def test_identical_bitmaps_share_aligned_bytes_and_keep_distinct_ids(self):
        resources = Resources([])
        ids = [resources.allocate(name) for name in ("FIRST", "SECOND", "THIRD")]
        for resource_id, data in zip(ids, (b"same", b"same", b"other")):
            resources.add("BMap", resource_id, data)
        source = "\n".join(
            [
                "#include <assert.h>",
                '#include "resources.h"',
                *emit_resources(resources),
                "int main(void) {",
                "assert(cfw_resource_count == 3);",
                "assert(cfw_resources[0].id != cfw_resources[1].id);",
                "assert(cfw_resources[0].data == cfw_resources[1].data);",
                "assert(cfw_resources[0].data != cfw_resources[2].data);",
                "for (unsigned i = 0; i < 3; i++)",
                "assert((uintptr_t)cfw_resources[i].data % 4 == 0);",
                "return 0;",
                "}",
            ]
        )
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "resources"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-x",
                    "c",
                    "-I",
                    str(Path(__file__).resolve().parents[2] / "payload"),
                    "-",
                    "-o",
                    str(binary),
                ],
                input=source,
                text=True,
                check=True,
            )
            subprocess.run([binary], check=True)

    def test_bitmap_sharing_preserves_template_copies_and_other_resource_types(self):
        resources = Resources([])
        resources.add("BMap", 1, bytes(4))
        resources.add("BMap", 2, Resource.template(Template(0x100, 4, {})))
        resources.add("BMap", 3, Resource.template(Template(0x200, 4, {})))
        resources.add("Str ", 4, bytes(4))
        self.assertEqual(len(set(resource_symbols(resources).values())), 4)
        code = "\n".join(emit_resources(resources))
        self.assertIn("0x100 + OSOS_RUNTIME_BASE", code)
        self.assertIn("0x200 + OSOS_RUNTIME_BASE", code)

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
