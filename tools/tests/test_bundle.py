#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Export rejection cases; Rust tests cover authenticated Python export replay."""

import copy
import importlib.util
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE = importlib.util.spec_from_file_location("bundle", ROOT / "tools/bundle.py")
bundle = importlib.util.module_from_spec(MODULE)
MODULE.loader.exec_module(bundle)


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.spec = bundle.helper_spec(
            ROOT / "usb-helper/tests/fixtures", "0.1.0-dev.1", "0.1.0"
        )

    def test_helper_without_storage_inspection_is_rejected(self):
        fixture = ROOT / "usb-helper/tests/fixtures"
        image = (fixture / "upload.dfu").read_bytes()
        descriptor = json.loads((fixture / "manifest.json").read_text())
        for value in (None, False, "true"):
            descriptor["storage_inspection"] = value
            with (
                self.subTest(value=value),
                self.assertRaisesRegex(ValueError, "storage inspection"),
            ):
                bundle.validate_helper(image, json.dumps(descriptor).encode())

    def test_existing_output_is_preserved(self):
        output = self.root / "existing"
        output.mkdir()
        (output / "keep").write_bytes(b"original")
        with self.assertRaises(ValueError):
            bundle.export(self.spec, self.root, output)
        self.assertEqual((output / "keep").read_bytes(), b"original")
        self.assertEqual(len(list(output.iterdir())), 1)

    def test_rejects_incomplete_release_and_invalid_versions(self):
        spec = copy.deepcopy(self.spec)
        spec["purpose"] = "release"
        with self.assertRaises(ValueError):
            bundle.export(spec, self.root, self.root / "release")
        self.assertFalse((self.root / "release").exists())
        for version in ["latest", "01.2.3", "1.0.0-01", "1.0.0+", "../escape"]:
            with self.subTest(version=version):
                spec = copy.deepcopy(self.spec)
                spec["version"] = version
                with self.assertRaises(ValueError):
                    bundle.export(spec, self.root, self.root / "invalid")

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

    def test_complete_envelope_and_deduplication(self):
        spec = copy.deepcopy(self.spec)
        spec["version"] = "1.0.0-dev.1+local.01"
        spec["purpose"] = "release"
        data = self.root / "synthetic"
        data.write_bytes(b"synthetic component fixture")
        for name, (fmt, required) in bundle.FORMATS.items():
            if name != "usb_helper":
                spec["components"][name] = {
                    "format": fmt,
                    "files": {key: str(data) for key in required},
                }
        result = bundle.export(spec, self.root, self.root / "complete")
        self.assertEqual(set(result["components"]), set(bundle.FORMATS))
        self.assertEqual(len(result["assets"]), 3)
        self.assertEqual(len(list((self.root / "complete").glob("*.blob"))), 3)

    def test_zip_contains_only_bundle_files_and_preserves_existing_output(self):
        directory = self.root / "bundle"
        manifest = bundle.export(self.spec, self.root, directory)
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
            with self.assertRaises(FileExistsError):
                bundle.pack(directory, output)
            self.assertEqual(output.read_bytes(), original)

    def test_zip_rejects_corruption_before_creating_output(self):
        directory = self.root / "bundle"
        manifest = bundle.export(self.spec, self.root, directory)
        digest = next(iter(manifest["assets"]))
        (directory / f"{digest}.blob").write_bytes(b"corrupt")
        output = self.root / "bundle.zip"
        with self.assertRaises(ValueError):
            bundle.pack(directory, output)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
