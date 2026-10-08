# SPDX-License-Identifier: GPL-3.0-only
"""Regression checks for dependency and release output preservation."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import release
import setup


class BuildTests(unittest.TestCase):
    def test_release_replaces_owned_artifacts_and_preserves_device_builds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, destination = root / "output", root / "build"
            output.mkdir()
            destination.mkdir()
            (destination / "MB565").mkdir()
            (destination / "MB565/osos-cfw.bin").write_bytes(b"keep")
            (destination / "repriseos.zip").write_bytes(b"old")
            (output / "repriseos.zip").write_bytes(b"new")
            (output / "package").mkdir()
            (output / "package/manifest.json").write_bytes(b"manifest")
            (destination / "package").symlink_to(destination / "MB565")
            with self.assertRaises(ValueError):
                release.publish(output, destination)
            self.assertEqual((destination / "repriseos.zip").read_bytes(), b"old")
            (destination / "package").unlink()
            rename = Path.rename

            def fail_package(source, target):
                if source.parent.name == "incoming" and source.name == "package":
                    raise OSError("publication interrupted")
                return rename(source, target)

            with patch.object(Path, "rename", fail_package), self.assertRaises(OSError):
                release.publish(output, destination)
            self.assertEqual((destination / "repriseos.zip").read_bytes(), b"old")
            self.assertFalse((destination / "package").exists())
            release.publish(output, destination)
            self.assertEqual((destination / "repriseos.zip").read_bytes(), b"new")
            self.assertEqual((destination / "MB565/osos-cfw.bin").read_bytes(), b"keep")
            self.assertEqual(
                (destination / "package/manifest.json").read_bytes(), b"manifest"
            )

    def test_dependency_check_requires_the_patch_and_prepare_can_apply_it(self):
        with (
            tempfile.TemporaryDirectory() as directory,
            patch.object(setup, "ROOT", Path(directory)),
        ):
            root = Path(directory)
            repo = root / "vendor/upstream"
            repo.mkdir(parents=True)
            subprocess.run(["git", "init", "-q", str(repo)], check=True)
            source = repo / "source.c"
            source.write_text("original\n")
            setup.git(repo, "add", "source.c")
            setup.git(
                repo,
                "-c",
                "user.name=Test",
                "-c",
                "user.email=test@example.invalid",
                "commit",
                "-qm",
                "baseline",
            )
            source.write_text("patched\n")
            (root / "change.patch").write_text(setup.git(repo, "diff"))
            source.write_text("original\n")
            spec = {
                "commit": setup.git(repo, "rev-parse", "HEAD").strip(),
                "patches": ["change.patch"],
            }
            with self.assertRaisesRegex(ValueError, "required patch"):
                setup.verify("upstream", spec)
            setup.prepare("upstream", spec)
            self.assertEqual(setup.verify("upstream", spec), repo)
            source.write_text("unrelated edit\n")
            with self.assertRaisesRegex(ValueError, "required patch"):
                setup.verify("upstream", spec)
            self.assertEqual(source.read_text(), "unrelated edit\n")
