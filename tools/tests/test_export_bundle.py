# SPDX-License-Identifier: GPL-3.0-only
"""Release packaging requires compiled recipes, not Apple inputs or full images."""

import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import export_bundle

ROOT = Path(__file__).resolve().parents[2]


class ExportBundleTests(unittest.TestCase):
    def test_export_without_apple_inputs_or_assembled_disk_images(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = b"bootloader code!"
            ipod = struct.pack(">I", 71 + sum(code)) + b"ip6g" + code
            (root / "bootloader-ipod6g.ipod").write_bytes(ipod)
            image = bytearray(0xB10)
            image[:8] = b"87021.0\x03"
            image[0x310:0x318] = b"87021.0\x02"
            struct.pack_into("<I", image, 0x31C, len(code))
            image.extend(code)
            (root / "install-rockbox-cfw.dfu").write_bytes(image)
            for name in ("osos", "companion"):
                (root / (name + ".json")).write_text(json.dumps({"recipe": name}))
                (root / (name + ".data")).write_bytes(name.encode())
            manifest = export_bundle.export(
                root,
                ROOT / "usb-helper/tests/fixtures",
                "0.1.0-test",
                "0.1.0",
                root / "bundle",
            )
            self.assertEqual(
                set(manifest["components"]), {"osos", "companion", "nor", "usb_helper"}
            )
            for name in ("osos", "companion"):
                digest = manifest["components"][name]["files"]["data"]
                self.assertEqual(
                    (root / "bundle" / (digest + ".blob")).read_bytes(), name.encode()
                )
            self.assertFalse((root / "inputs").exists())
            self.assertFalse((root / "cfw-loader.bin").exists())
