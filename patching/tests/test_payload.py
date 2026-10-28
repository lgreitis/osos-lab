# SPDX-License-Identifier: GPL-3.0-only
"""Sanitized host checks for native payload contracts."""

import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from patching.ui import Resources, generate
from patching.ui.codegen import emit_resources


@unittest.skipUnless(shutil.which("cc"), "Host C compiler is required")
class PayloadTests(unittest.TestCase):
    def test_embedded_bitmaps_have_native_word_alignment(self):
        root = Path(__file__).resolve().parents[2]
        resources = Resources([])
        resources.add("Str ", resources.allocate("Padding"), b"xy\0")
        resources.add("BMap", resources.allocate("Icon"), bytes(32))
        source = "\n".join(
            [
                '#include "resources.h"',
                *emit_resources(resources),
                '_Static_assert(__alignof__(resource_1) >= 4, "Native BMap alignment");',
            ]
        )
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsyntax-only",
                "-x",
                "c",
                "-I",
                str(root / "payload"),
                "-",
            ],
            input=source,
            text=True,
            check=True,
        )

    def test_native_contracts(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        with tempfile.TemporaryDirectory(prefix="payload-test-") as temporary:
            directory = Path(temporary)
            generate(
                Resources(metadata["resources"], bindings),
                [
                    root / "payload/dark_mode.ui",
                    root / "payload/custom_eq.ui",
                    root / "payload/cfw_info.ui",
                ],
                directory,
                "test",
            )
            for name in (
                "album_artist",
                "album_artists_index",
                "ui_visibility",
                "ui_model",
                "eq_preferences",
                "eq_audio",
                "eq_canvas",
                "menu_resources",
                "bitmap_assets",
                "dark_mode",
                "osos_handoff",
            ):
                with self.subTest(fixture=name):
                    executable = directory / name
                    subprocess.run(
                        [
                            "cc",
                            "-std=c11",
                            "-Wall",
                            "-Wextra",
                            "-Werror",
                            "-fsanitize=address,undefined",
                            "-g",
                            "-I",
                            str(directory),
                            "-I",
                            str(root / "payload"),
                            str(Path(__file__).parent / "fixtures" / f"{name}.c"),
                            "-o",
                            str(executable),
                        ],
                        check=True,
                    )
                    subprocess.run([str(executable)], check=True)
