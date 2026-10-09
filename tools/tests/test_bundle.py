#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Python export/pack integration; Rust owns bundle validation tests."""

import copy
import hashlib
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import bundle  # noqa: E402


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.spec = bundle.helper_spec(
            ROOT / "usb-helper/tests/fixtures", "0.2.0-dev.1", "0.2.0"
        )

    def test_export_checks_helper_structure_after_matching_its_hash(self):
        fixture = ROOT / "usb-helper/tests/fixtures"
        image = bytearray((fixture / "upload.dfu").read_bytes())
        descriptor = json.loads((fixture / "manifest.json").read_text())
        image[0] ^= 1
        descriptor["sha256"] = hashlib.sha256(image).hexdigest()
        (self.root / "upload.dfu").write_bytes(image)
        (self.root / "manifest.json").write_text(json.dumps(descriptor))
        spec = bundle.helper_spec(self.root, "0.2.0", "0.2.0")
        output = self.root / "invalid"
        with self.assertRaisesRegex(ValueError, "image/manifest mismatch"):
            bundle.export(spec, self.root, output)
        self.assertFalse(output.exists())

    def test_missing_input_and_symlink_publish_nothing(self):
        source = self.root / "input"
        for symlink in [False, True]:
            spec = copy.deepcopy(self.spec)
            if symlink:
                source.symlink_to(ROOT / "usb-helper/tests/fixtures/upload.dfu")
            spec["components"]["usb_helper"]["files"]["image"] = str(source)
            with self.assertRaises(ValueError):
                bundle.export(spec, self.root, self.root / "out")
            self.assertFalse((self.root / "out").exists())

    def test_zip_contains_only_bundle_files_and_preserves_existing_output(self):
        directory = self.root / "bundle"
        manifest = bundle.export(self.spec, self.root, directory)
        self.assertEqual(manifest["schema"], 2)
        self.assertEqual(
            manifest["compatibility"]["hardware_versions"],
            [0x130000, 0x130100, 0x130200, 0x130300],
        )
        (directory / "private-key").write_bytes(b"excluded")
        for signed in [False, True]:
            if signed:
                (directory / "manifest.json.sig").write_bytes(bytes(64))
            output = self.root / f"bundle-{signed}.zip"
            bundle.pack(directory, output)
            with zipfile.ZipFile(output) as archive:
                expected = {"manifest.json", *(f"{h}.blob" for h in manifest["assets"])}
                if signed:
                    expected.add("manifest.json.sig")
                self.assertEqual(set(archive.namelist()), expected)
                for name in expected:
                    self.assertEqual(
                        archive.read(name), (directory / name).read_bytes()
                    )
            original = output.read_bytes()
            with self.assertRaises(ValueError):
                bundle.pack(directory, output)
            self.assertEqual(output.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
