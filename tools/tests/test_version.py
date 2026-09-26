# SPDX-License-Identifier: GPL-3.0-only

import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from version import identity


class VersionTests(unittest.TestCase):
    def resolve(self, tags="", dirty="", tag=None):
        with patch(
            "version.subprocess.check_output", side_effect=["1234abcd", dirty, tags]
        ):
            return identity(Path("source"), tag)

    def test_release_and_development_identity(self):
        self.assertEqual(self.resolve("v0.1.0-alpha.1")["version"], "0.1.0-alpha.1")
        self.assertEqual(self.resolve()["version"], "0.0.0-dev+1234abcd")
        dirty = self.resolve("v0.1.0", " M payload/eq.c")
        self.assertEqual(dirty["version"], "0.0.0-dev+1234abcd-dirty")
        self.assertEqual(dirty["revision"], "1234abcd-dirty")

    def test_release_requires_an_unambiguous_clean_matching_tag(self):
        for tags, dirty, tag in [
            ("", "", "v0.1.0"),
            ("v0.1.0", " M payload/eq.c", "v0.1.0"),
            ("v0.1.0", "", "v0.1.0-01"),
            ("v0.1.0", "", "0.1.0"),
            ("v0.1.0\nv0.2.0", "", None),
        ]:
            with self.subTest(tag=tag, dirty=dirty), self.assertRaises(ValueError):
                self.resolve(tags, dirty, tag)
        self.assertEqual(
            self.resolve("v0.1.0\nv0.2.0", tag="v0.2.0")["version"], "0.2.0"
        )
