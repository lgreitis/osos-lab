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
        self.check_fixture("album_artist.c")

    @unittest.skipUnless(shutil.which("cc"), "Host C compiler is required")
    def test_browser_index_preserves_groups_and_native_album_order(self):
        self.check_fixture("album_artists_index.c")

    @unittest.skipUnless(shutil.which("cc"), "Host C compiler is required")
    def test_menu_indices_and_saved_visibility(self):
        self.check_fixture("album_artists_menu.c")

    @unittest.skipUnless(shutil.which("cc"), "Host C compiler is required")
    def test_menu_visibility_uses_installed_resource_rows(self):
        self.check_fixture("menu_resources.c")

    def check_fixture(self, name):
        fixture = Path(__file__).parent / "fixtures" / name
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
