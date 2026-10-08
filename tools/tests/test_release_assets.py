# SPDX-License-Identifier: GPL-3.0-only
"""Signed Classic release layout and incomplete-release rejection."""

import hashlib
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import bundle
import release_assets as release


class ReleaseAssetsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.directory = self.root / "build"
        self.output = self.root / "assets"
        self.reprise = release.ROOT / "host/target/debug/reprise"
        self.seed, self.key = self.root / "seed", self.root / "key"
        self.seed.write_text("17" * 32)
        # Recipes are opaque to collection; assembly has separate integration tests.
        (self.root / "component").write_bytes(b"test component")
        for target in ("classic",):
            spec = bundle.helper_spec(
                release.ROOT / "usb-helper/tests/fixtures", "0.1.0", "0.1.0"
            )
            spec["purpose"] = "release"
            for name, format_name, fields in (
                ("osos", "reprise-osos-recipe-v2", ("recipe", "data")),
                ("companion", "reprise-companion-recipe-v2", ("recipe", "data")),
                ("nor", "reprise-nor-template-v1", ("image", "descriptor")),
            ):
                spec["components"][name] = {
                    "format": format_name,
                    "files": dict.fromkeys(fields, "component"),
                }
            package = self.directory / target / "package"
            bundle.export(spec, self.root, package)
        report = release.invoke(
            self.reprise, "sign", "--directory", package, "--seed", self.seed
        )
        self.key.write_text(report["public_key"])
        (package / "manifest.json.sig").unlink()
        for name in ("osos-lab-source.tar.gz", "rockbox-source.tar.gz"):
            (self.directory / name).write_bytes(b"test source archive")

    def collect(self, version="0.1.0"):
        release.collect(
            self.directory, self.output, self.reprise, self.seed, self.key, version
        )

    def test_signed_release_blobs_and_zip_manifest_match(self):
        self.collect()
        hashes = set()
        for target in ("classic",):
            raw = (self.output / f"{target}-manifest.json").read_bytes()
            manifest = json.loads(raw)
            self.assertEqual(manifest["compatibility"]["target"], target)
            hashes.update(manifest["assets"])
            with zipfile.ZipFile(self.output / f"repriseos-{target}.zip") as archive:
                self.assertEqual(archive.read("manifest.json"), raw)
                self.assertEqual(
                    archive.read("manifest.json.sig"),
                    (self.output / f"{target}-manifest.json.sig").read_bytes(),
                )
                extracted = self.root / target
                archive.extractall(extracted)
            release.invoke(
                self.reprise, "inspect", "--directory", extracted, "--key", self.key
            )
            self.assertFalse(
                (self.directory / target / "package/manifest.json.sig").exists()
            )
        self.assertEqual({p.stem for p in self.output.glob("*.blob")}, hashes)
        lines = (self.output / "SHA256SUMS").read_text().splitlines()
        self.assertEqual(len(lines), len(list(self.output.iterdir())) - 1)
        for line in lines:
            digest, name = line.split("  ")
            self.assertEqual(
                hashlib.sha256((self.output / name).read_bytes()).hexdigest(), digest
            )

    def test_failed_collection_leaves_no_partial_release(self):
        with self.assertRaisesRegex(ValueError, "identity mismatch"):
            self.collect("0.2.0")
        self.assertFalse(self.output.exists())
        package = self.directory / ("classic",)[-1] / "package"
        (package / "manifest.json").unlink()
        with self.assertRaises(ValueError):
            self.collect()
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
