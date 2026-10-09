# SPDX-License-Identifier: GPL-3.0-only
"""Sanitized host checks for native payload contracts."""

import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from patching.ui import Resources, generate


@unittest.skipUnless(shutil.which("cc"), "Host C compiler is required")
class PayloadTests(unittest.TestCase):
    def test_native_contracts(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        with tempfile.TemporaryDirectory(prefix="payload-test-") as temporary:
            directory = Path(temporary)
            generate(
                Resources(metadata["resources"], bindings),
                [root / "payload/dark_mode.ui", root / "payload/custom_eq.ui"],
                directory,
                "test",
            )
            for name in (
                "album_artist",
                "album_artists_index",
                "album_artists_menu",
                "menu_resources",
                "bitmap_assets",
                "dark_mode",
            ):
                with self.subTest(fixture=name):
                    executable = directory / name
                    flags = []
                    if name == "bitmap_assets":
                        # Native pointers have four-byte alignment on a 64-bit host.
                        flags.append("-fno-sanitize=alignment")
                    subprocess.run(
                        [
                            "cc",
                            "-std=c11",
                            "-Wall",
                            "-Wextra",
                            "-Werror",
                            "-fsanitize=address,undefined",
                            "-g",
                            *flags,
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
