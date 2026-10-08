# SPDX-License-Identifier: GPL-3.0-only
"""Host checks for retained metadata ownership and importer cursor handling."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class AlbumArtistTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "Host C compiler is required")
    def test_retained_metadata(self):
        fixture = Path(__file__).parent / "fixtures/album_artist.c"
        with tempfile.TemporaryDirectory(prefix="album-artist-test-") as directory:
            executable = Path(directory) / "test"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-g",
                    str(fixture),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run(
                [str(executable)], check=True, capture_output=True, text=True
            )
