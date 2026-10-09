# SPDX-License-Identifier: GPL-3.0-only
"""Import boundaries and publication failures must preserve existing work."""

import argparse
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ghidra_analysis
import ghidra_project

import ghidra


class WorkflowTests(unittest.TestCase):
    def test_firmware_analysis_profile_does_not_require_device_identity(self):
        profile = ghidra.load_profile("firmware-2.0.5")
        self.assertEqual(profile["ipsw"]["version"], "2.0.5")
        self.assertNotIn("compatibility", profile)
        self.assertNotIn("nor.bin", profile["inputs"])
        for name in ("classic7g-2.0.4", "classic6g-reva-2.0.1"):
            self.assertIn("analysis", ghidra.load_profile(name))
        with self.assertRaisesRegex(ValueError, "Invalid analysis target"):
            ghidra.load_profile("../firmware-2.0.5")

    def test_selection_requires_distinct_known_targets(self):
        manifest = json.loads(ghidra.MANIFEST.read_text())
        name = "classic6g-reva-2.0.1"
        selected = ghidra.select_targets(manifest, [name])
        self.assertEqual(len(selected["programs"]), 41)
        self.assertTrue(
            all(s["path"].startswith("2.0.1/") for s in selected["programs"])
        )
        for names in ([name, name], ["unknown"]):
            with self.assertRaises(ValueError):
                ghidra.select_targets(manifest, names)

    def test_failed_verification_does_not_publish_project(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "project"
            with self.assertRaisesRegex(RuntimeError, "verification"):
                with ghidra_project.staged_project(destination) as stage:
                    (stage / "osos.gpr").write_text("partial")
                    raise RuntimeError("verification failed")
            self.assertFalse(destination.exists())
            self.assertEqual(list(Path(temporary).iterdir()), [])
            with ghidra_project.staged_project(destination) as stage:
                (stage / "osos.gpr").write_text("verified")
            self.assertEqual((destination / "osos.gpr").read_text(), "verified")

    def test_export_publication_failure_restores_annotations(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            destination, source = root / "analysis", root / "new"
            destination.mkdir()
            (destination / "saved").write_text("annotations")
            source.mkdir()
            (source / "program.json").write_text("{}")
            rename = Path.rename

            def fail_publish(path, target):
                if path.name == "ready":
                    raise OSError("publication failed")
                return rename(path, target)

            with patch.object(Path, "rename", fail_publish):
                with self.assertRaisesRegex(OSError, "publication failed"):
                    ghidra_analysis.publish_analysis(source, destination)
            self.assertEqual((destination / "saved").read_text(), "annotations")
            self.assertFalse((destination / "program.json").exists())
            ghidra_analysis.publish_analysis(source, destination)
            self.assertTrue((destination / "program.json").exists())
            self.assertEqual((destination / "saved").read_text(), "annotations")

    def test_staging_never_replaces_destination(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "project"
            destination.mkdir()
            saved = destination / "saved"
            saved.write_text("annotations")
            with self.assertRaises(ValueError):
                with ghidra_project.staged_project(destination):
                    pass
            self.assertEqual(saved.read_text(), "annotations")

    def test_encrypted_input_prerequisites_and_ambiguous_inputs(self):
        args = argparse.Namespace(
            command="import",
            ipsw=Path("firmware.ipsw"),
            aupd=Path("nor.bin"),
            osos=None,
            inputs=None,
            target=None,
            fresh=False,
        )
        with self.assertRaisesRegex(ValueError, "decrypted 2.0.5 OSOS"):
            ghidra.validate_inputs(args)
        args.osos = Path("plaintext.bin")
        ghidra.validate_inputs(args)
        args.ipsw = args.aupd = args.osos = None
        args.inputs = Path("inputs")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            ghidra.validate_inputs(args)

    def test_manifest_entries_have_explicit_modes_in_executable_regions(self):
        manifest = json.loads(ghidra.MANIFEST.read_text())
        for spec in manifest["programs"]:
            executable = [r for r in spec["regions"] if r["flags"] & 1]
            self.assertTrue(
                set(spec["thumb_regions"]) <= {r["name"] for r in executable}
            )
            for entry in spec["entry_points"]:
                self.assertEqual(entry["address"] & 1, 0)
                self.assertIsInstance(entry["thumb"], bool)
                self.assertTrue(
                    any(
                        r["address"] <= entry["address"] < r["address"] + r["size"]
                        for r in executable
                    ),
                    spec["path"],
                )

    def test_project_preflight_rejects_existing_import_and_locked_export(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary)
            args = argparse.Namespace(command="import", project=project, name="osos")
            with self.assertRaisesRegex(ValueError, "new project directory"):
                ghidra_project.check_project(args)
            args.command = "export"
            (project / "osos.gpr").touch()
            (project / "osos.lock").touch()
            with self.assertRaisesRegex(ValueError, "lock present"):
                ghidra_project.check_project(args)

    def test_explicit_target_must_match_ipsw(self):
        args = argparse.Namespace(
            reprise=Path("reprise"),
            ipsw=Path("firmware.ipsw"),
            target=["classic7g-2.0.4"],
        )
        with patch("ghidra.subprocess.run") as run:
            run.return_value.stdout = json.dumps({"version": "2.0.5"})
            with self.assertRaisesRegex(ValueError, "does not match"):
                ghidra.resolve_ipsw(args)
            args.target = None
            ghidra.resolve_ipsw(args)
            self.assertEqual(args.target, ["firmware-2.0.5"])

    def test_missing_completion_marker_is_failure_even_with_zero_exit(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = argparse.Namespace(ghidra=Path(temporary))

            def run(command, stdout, **kwargs):
                stdout.write("INFO Import finished\n")
                return argparse.Namespace(returncode=0)

            with (
                patch.object(ghidra_project, "WORK", Path(temporary)),
                patch("ghidra_project.subprocess.run", side_effect=run),
            ):
                with self.assertRaisesRegex(RuntimeError, "completion markers"):
                    ghidra_project.headless(
                        args, Path(temporary), "osos", [], "test", ["VERIFIED "]
                    )

    def test_arm_context_conflict_is_reported_but_other_exceptions_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = argparse.Namespace(ghidra=Path(temporary))
            conflict = (
                "ERROR Unexpected Exception (ArmAnalyzer) "
                "ghidra.program.model.listing.ContextChangeException: "
                "Context register change conflicts with one or more instructions"
            )

            def run(command, stdout, **kwargs):
                stdout.write(message + "\nPREPARED firmware\n")
                return argparse.Namespace(returncode=0)

            with (
                patch.object(ghidra_project, "WORK", Path(temporary)),
                patch("ghidra_project.subprocess.run", side_effect=run),
            ):
                message = conflict
                ghidra_project.headless(
                    args, Path(temporary), "osos", [], "test", ["PREPARED firmware"]
                )
                message = "ERROR Unexpected Exception (ArmAnalyzer) RuntimeException"
                with self.assertRaisesRegex(RuntimeError, "RuntimeException"):
                    ghidra_project.headless(
                        args, Path(temporary), "osos", [], "test", ["PREPARED firmware"]
                    )


if __name__ == "__main__":
    unittest.main()
