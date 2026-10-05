# SPDX-License-Identifier: GPL-3.0-only
"""Build-selected bindings and optional saved-input firmware integration."""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from patching.compatibility import companion_shims, shim_directory
from patching.recipes.companion import build_recipe
from patching.recipes.osos import build_recipe as build_osos_recipe

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "loader/apple"
TARGETS = ("classic7g-2.0.4", "classic6g-reva-2.0.1")


class CompatibilityTests(unittest.TestCase):
    def test_manifest_selects_existing_independent_bindings(self):
        for target in TARGETS:
            profile = ROOT / "targets" / f"{target}.json"
            shims = companion_shims(SOURCE, profile)
            self.assertEqual(shims.nor.parent, SOURCE / "compat/nor")
            self.assertEqual(shims.osos.parent, ROOT / "payload/compat/osos")
            self.assertTrue((shims.nor / "nor-target.h").is_file())
            self.assertTrue((shims.osos / "osos-layout.h").is_file())

    def test_unknown_or_escaping_binding_has_no_fallback(self):
        for name in (None, "", ".", "..", "../2.0.4", "/2.0.4", "9.9.9"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                shim_directory(SOURCE, "nor", name, "nor-target.h")


@unittest.skipUnless(
    os.environ.get("REPRISE_ASSEMBLY_ROOT"),
    "Set REPRISE_ASSEMBLY_ROOT to run saved-input ARM/Rust integration",
)
class SavedFirmwareTests(unittest.TestCase):
    def assert_rejected(self, command, message):
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(message, result.stderr)
        self.assertFalse(Path(command[command.index("--out") + 1]).exists())

    def test_both_payloads_replay_and_reject_wrong_inputs_and_preimages(self):
        root = Path(os.environ["REPRISE_ASSEMBLY_ROOT"]).resolve()
        prefix = str(root / "toolchains/rockbox-arm/bin/arm-elf-eabi-")
        reprise = root / "host/target/release/reprise"
        with tempfile.TemporaryDirectory(
            prefix="payload-test-", dir=root / ".build"
        ) as directory:
            for target, input_name in zip(TARGETS, ("", "MB565-2.0.1")):
                with self.subTest(target=target):
                    output = Path(directory) / target
                    recipe = build_osos_recipe(
                        root / "payload",
                        output / "compile",
                        root / "targets" / f"{target}.json",
                        prefix,
                        8,
                        "port-test",
                    )
                    files = recipe.save(output, "osos")
                    inputs = root / "inputs" / input_name
                    image = output / "osos-cfw.bin"
                    command = [
                        str(reprise),
                        "bundle",
                        "apply-recipe",
                        "--recipe",
                        files["recipe"],
                        "--data",
                        files["data"],
                        "--inputs",
                        str(inputs),
                        "--out",
                        str(image),
                    ]
                    result = subprocess.run(command, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(
                        image.stat().st_size,
                        (inputs / "osos.bin").stat().st_size + 0x100000,
                    )
                    original = image.read_bytes()
                    command[command.index("--out") + 1] = str(output / "rejected.bin")
                    wrong = command.copy()
                    wrong[wrong.index("--inputs") + 1] = str(
                        root / "inputs" / ("MB565-2.0.1" if not input_name else "")
                    )
                    self.assert_rejected(wrong, "fingerprint mismatch")
                    spec = json.loads(Path(files["recipe"]).read_text())
                    check = spec["checks"][0]
                    before = bytes.fromhex(check["hex"])
                    check["hex"] = (bytes([before[0] ^ 1]) + before[1:]).hex()
                    Path(files["recipe"]).write_text(json.dumps(spec))
                    self.assert_rejected(command, "preimage mismatch")
                    self.assertEqual(image.read_bytes(), original)

    def test_both_companions_replay_and_reject_wrong_patch_bytes(self):
        root = Path(os.environ["REPRISE_ASSEMBLY_ROOT"]).resolve()
        prefix = str(root / "toolchains/rockbox-arm/bin/arm-elf-eabi-")
        reprise = root / "host/target/release/reprise"
        work = root / ".build"
        work.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="shim-test-", dir=work) as directory:
            for target, input_name in zip(TARGETS, ("", "MB565-2.0.1")):
                with self.subTest(target=target):
                    output = Path(directory) / target
                    recipe = build_recipe(
                        root / "loader/apple",
                        output / "compile",
                        root / "targets" / f"{target}.json",
                        prefix,
                        8,
                    )
                    files = recipe.save(output, "companion")
                    inputs = root / "inputs" / input_name
                    image = output / "cfw-loader.bin"
                    command = [
                        str(reprise),
                        "bundle",
                        "apply-recipe",
                        "--recipe",
                        files["recipe"],
                        "--data",
                        files["data"],
                        "--inputs",
                        str(inputs),
                        "--nor",
                        str(inputs / "nor.bin"),
                        "--out",
                        str(image),
                    ]
                    result = subprocess.run(command, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(image.stat().st_size, 0x2B800)
                    original = image.read_bytes()
                    result = subprocess.run(command, capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("Output already exists", result.stderr)
                    self.assertEqual(image.read_bytes(), original)
                    command[command.index("--out") + 1] = str(output / "rejected.bin")

                    wrong_inputs = (
                        root / "inputs" / ("MB565-2.0.1" if not input_name else "")
                    )
                    mismatch = command.copy()
                    mismatch[mismatch.index("--inputs") + 1] = str(wrong_inputs)
                    self.assert_rejected(mismatch, "fingerprint mismatch")

                    mismatch = command.copy()
                    mismatch[mismatch.index("--nor") + 1] = str(
                        wrong_inputs / "nor.bin"
                    )
                    self.assert_rejected(mismatch, "different targets")

                    path = Path(files["recipe"])
                    spec = json.loads(path.read_text())
                    check = spec["checks"][0]
                    before = bytes.fromhex(check["hex"])
                    check["hex"] = (bytes([before[0] ^ 1]) + before[1:]).hex()
                    path.write_text(json.dumps(spec))
                    self.assert_rejected(command, "preimage mismatch")
                    self.assertEqual(image.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
