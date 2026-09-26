# SPDX-License-Identifier: GPL-3.0-only
"""Exercise authoring through a clean input-free build directory."""

import json
import os
import shutil
import struct
import tempfile
import unittest
from pathlib import Path

from patching import declarations, native, ui
from patching.companion import build_recipe as build_companion_recipe
from patching.declarations import Copy, Kind, Write
from patching.osos import arm_call
from patching.osos_build import build_recipe
from patching.symbols import read as read_symbols
from patching.toolchain import run

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which(
    os.environ.get("CROSS_COMPILE", "arm-elf-eabi-") + "gcc"
) or shutil.which(str(ROOT.parent / "toolchains/rockbox-arm/bin/arm-elf-eabi-gcc"))


@unittest.skipUnless(COMPILER, "ARM toolchain not installed")
class BuildTests(unittest.TestCase):
    def test_loader_hook_copy_and_placement_changes_need_only_assembly_and_linker(self):
        with tempfile.TemporaryDirectory(prefix="reprise-companion-test-") as temporary:
            root = Path(temporary)
            source = root / "loader/apple"
            shutil.copytree(ROOT / "loader/apple", source)
            (root / "payload").mkdir()
            shutil.copyfile(ROOT / "payload/layout.h", root / "payload/layout.h")
            target = root / "target.json"
            shutil.copyfile(ROOT / "targets/classic7g-2.0.4.json", target)
            linker = source / "startup/probe.lds"
            linker.write_text(linker.read_text().replace("0x2201d900", "0x2201d8e0"))
            startup = source / "startup/start.S"
            startup.write_text(
                startup.read_text().replace(
                    "0x10574, cold_guard,", "0x10574, example_hook,"
                )
                + "\n.thumb\n.thumb_func\nexample_hook:\n    bx lr\n"
                + ".section .data\n.balign 4\nexample_copy:\n"
                + "native_copy osos, 0, 4\n"
            )
            recipe = build_companion_recipe(
                source, root / "work", target, COMPILER[:-3], 8
            )
            self.assertEqual(recipe.size, 0x2B800)
            self.assertEqual(
                set(recipe.relocations),
                {"rom", "clockandreset", "cpu", "interruptcontroller", "bds"},
            )
            self.assertEqual(recipe.ffs_checksums, [0xE1E0])
            self.assertEqual(recipe.relocations["clockandreset"]["base"], 0x2201D8E0)
            position = 0
            for segment in recipe.segments:
                if segment.get("name") == "clockandreset":
                    self.assertEqual(position, 0x1D8E0)
                    break
                position += segment["bytes"]
            else:
                self.fail("Linked ClockAndReset reservation was not copied")
            linked = read_symbols(root / "work/startup/probe.elf", COMPILER[:-3])
            position = 0
            found_hook = found_copy = False
            for segment in recipe.segments:
                size = segment["bytes"]
                if position <= 0x10574 < position + size:
                    self.assertEqual(segment["kind"], "data")
                    offset = segment["offset"] + 0x10574 - position
                    high, low = struct.unpack_from("<HH", recipe.data, offset)
                    self.assertEqual((high >> 11, low >> 11), (0x1E, 0x1F))
                    delta = ((high & 0x7FF) << 12) | ((low & 0x7FF) << 1)
                    if delta & (1 << 22):
                        delta -= 1 << 23
                    self.assertEqual(0x22010578 + delta, linked["example_hook"] & ~1)
                    found_hook = True
                if position == linked["example_copy"] - 0x22000000:
                    self.assertEqual(
                        segment,
                        {"kind": "input", "name": "osos", "offset": 0, "bytes": 4},
                    )
                    found_copy = True
                position += size
            self.assertTrue(found_hook and found_copy)
            self.assertFalse((root / "inputs").exists())
            self.assertFalse(list((root / "work").rglob("*-relocated.bin")))
            files = recipe.save(root / "out", "companion")
            self.assertLess(Path(files["data"]).stat().st_size, 0x8000)

            startup = root / "work/startup"
            run(
                [
                    COMPILER[:-3] + "objcopy",
                    "--remove-section=.cfw.native",
                    "probe.elf",
                ],
                startup,
            )
            with self.assertRaisesRegex(ValueError, "Missing native declarations"):
                native.collect(startup, "probe", COMPILER[:-3])

    def test_new_patch_page_and_reordered_menu_build_without_apple_inputs(self):
        with tempfile.TemporaryDirectory(prefix="reprise-recipe-test-") as temporary:
            root = Path(temporary)
            source, target, work = root / "payload", root / "targets", root / "work"
            shutil.copytree(ROOT / "payload", source)
            shutil.copytree(ROOT / "targets", target)
            (source / "example.c").write_text(
                '#include "patch.h"\n'
                "PATCH_CALL(0x08001000, 0x08002000, example);\n"
                "PATCH_ARM int example(int value) { return value + 1; }\n"
                "int thumb_handler(int value) { return value + 2; }\n"
                "const unsigned int table[3] = {[2] = 122};\n"
                "PATCH_COPY(table, 0, 0x083E2568, 8);\n"
            )
            (source / "example.ui").write_text(
                'UI_TEXT(EXAMPLE, "Example", "example.txt")\n'
                'UI_SETTINGS_ENTRY(EXAMPLE, 0x0DAD0C3F, "")\n'
            )
            (source / "example.txt").write_text("Extra page\n\nAnother paragraph")
            page = source / "cfw_info.ui"
            page.write_text(page.read_text().replace("CFW_Info", "TEST_Info"))
            menu = source / "custom_eq.ui"
            text = menu.read_text().replace(
                "    BAND(1, 1, 2, 60)\n    BAND(2, 5, 1, 1000)\n    BAND(3, 9, 3, 8000)",
                "    BAND(3, 9, 3, 8000)\n    BAND(1, 1, 2, 80)\n    BAND(2, 5, 1, 1000)",
            )
            menu.write_text(text)
            recipe = build_recipe(
                source, work, target / "classic7g-2.0.4.json", COMPILER[:-3], 8, "test"
            )
            files = recipe.save(root / "out", "osos")
            spec = json.loads(Path(files["recipe"]).read_text())
            self.assertEqual(spec["schema"], 2)
            self.assertNotIn("sha256", spec["output"])
            self.assertFalse((root / "inputs").exists())
            patches = declarations.parse((work / "patches.bin").read_bytes())
            self.assertIn(
                Write(
                    Kind.CALL,
                    0x08001000,
                    struct.pack("<I", arm_call(0x08001000, 0x08002000)),
                    symbol="example",
                ),
                patches,
            )
            self.assertIn(Copy("table", 0, 12, 0x083E2568, 8), patches)
            linked = read_symbols(work / "example.c.o", COMPILER[:-3])
            self.assertEqual(linked["example"] % 4, 0)
            self.assertEqual(linked["thumb_handler"] % 2, 1)
            self.assertEqual(
                read_symbols(work / "example.c.o", COMPILER[:-3], thumb_bits=False)[
                    "thumb_handler"
                ],
                linked["thumb_handler"] & ~1,
            )
            document = ui.parse((work / "custom_eq_declarations.bin").read_bytes())
            self.assertEqual(document.root.items[1].name, "CFW_EQ_Band3")
            self.assertEqual([item.slot for item in document.fields], list(range(13)))
            self.assertEqual(document.fields[2].default_index, 7)
            generated = (work / "ui_resources.c").read_text()
            self.assertIn("EXAMPLE_Screen", generated)
            self.assertIn("TEST_Info_Screen", generated)
            self.assertNotIn("CFW_Info_Screen", generated)
