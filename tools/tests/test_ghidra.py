# SPDX-License-Identifier: GPL-3.0-only
"""Regression checks for firmware memory mapping."""

import importlib.util
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


images = load("ghidra_images")
firmware = load("firmware")
prepare = load("ghidra_prepare")


class PreparationTests(unittest.TestCase):
    @unittest.skipUnless(
        (ROOT / "inputs/firmware-2.0.5/osos.bin").is_file(),
        "Saved 2.0.5 input required",
    )
    def test_205_mapping_covers_relocated_bytes_and_excludes_padding(self):
        data = (ROOT / "inputs/firmware-2.0.5/osos.bin").read_bytes()
        profile = prepare.load_profile("firmware-2.0.5")
        regions = images.osos_regions(data, profile)
        initialized = sorted(
            (r for r in regions if r["data"]), key=lambda r: r["source"]
        )
        self.assertEqual(b"".join(r["data"] for r in initialized), data[0x800:-4])
        self.assertEqual(data[-4:], bytes(4))
        bss = next(r for r in regions if r["name"] == ".dram.bss")
        self.assertEqual(bss["address"] + bss["size"], 0x08B3B1B0)
        changed = bytearray(data)
        changed[0x4FC4] ^= 4
        profile["inputs"]["osos.bin"]["sha256"] = images.sha(changed)
        with self.assertRaisesRegex(ValueError, "copy/clear constants"):
            images.osos_regions(changed, profile)

    def test_elf_load_segments_exclude_headers_and_bss_bytes(self):
        regions = [
            images.region(".iram", 0x22000000, b"IRAM", flags=5),
            images.region(".bss", 0x08000004, size=0x100),
            images.region(".dram", 0x08000000, b"DRAM", flags=7),
        ]
        data, slices = images.elf(regions, 0x22000000)
        self.assertEqual(struct.unpack_from("<HHI", data, 16), (2, 40, 1))
        self.assertEqual(struct.unpack_from("<I", data, 24)[0], 0x22000000)
        loads = [struct.unpack_from("<8I", data, 52 + i * 32) for i in range(3)]
        self.assertEqual(
            [(p[2], p[4], p[5]) for p in loads],
            [(0x08000000, 4, 4), (0x08000004, 0, 0x100), (0x22000000, 4, 4)],
        )
        self.assertEqual(b"".join(data[o : o + n] for o, n in slices), b"DRAMIRAM")
        for p in loads:
            if p[4]:
                self.assertGreaterEqual(p[1], 52 + 3 * 32)

    def test_te_stripped_headers_keep_in_place_runtime_addresses(self):
        data = bytearray(0x80)
        struct.pack_into(
            "<HHBBHIIQ", data, 0, 0x5A56, 0x14C, 1, 11, 0x178, 0x1A0, 0x1A0, 0x2201F52C
        )
        data[40:48] = b".text\0\0\0"
        struct.pack_into("<4I", data, 48, 0x30, 0x1A0, 0x30, 0x1A0)
        struct.pack_into("<I", data, 76, 0x60000020)
        regions, entry, mappings = images.pe_regions(bytes(data))
        self.assertEqual(entry, 0x2201F57C)
        self.assertEqual(regions[1]["data"], data[0x50:0x80])
        self.assertIn((0x50, 0x30, entry), mappings)

    def test_unknown_firmware_and_overlapping_regions_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "Input does not match firmware"):
            images.osos_regions(bytes(0x10000), prepare.load_profile("firmware-2.0.5"))
        with self.assertRaisesRegex(ValueError, "Overlapping"):
            images.elf(
                [images.region("a", 0, b"1234"), images.region("b", 2, b"56")], 0
            )


if __name__ == "__main__":
    unittest.main()
