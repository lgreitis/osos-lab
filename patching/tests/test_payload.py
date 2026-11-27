# SPDX-License-Identifier: GPL-3.0-only
"""Sanitized host checks for native payload contracts."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(shutil.which("cc"), "Host C compiler is required")
class PayloadTests(unittest.TestCase):
    def test_native_contracts(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory(prefix="payload-test-") as temporary:
            directory = Path(temporary)
            for name in (
                "media_menus",
                "ui_visibility",
                "rvx_compression",
                "store",
                "osos_handoff",
                "recovery",
                "panic_backlight",
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
                            str(root / "payload"),
                            str(Path(__file__).parent / "fixtures" / f"{name}.c"),
                            "-o",
                            str(executable),
                            "-lm",
                        ],
                        check=True,
                    )
                    subprocess.run([str(executable)], check=True)
