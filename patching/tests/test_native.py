# SPDX-License-Identifier: GPL-3.0-only
"""Linked module declarations must preserve compiled patches and source bounds."""

import unittest

from patching.native import RECORD, Declaration, Kind, apply, arm_b, parse, thumb_bl
from patching.recipe import Recipe


class NativeTests(unittest.TestCase):
    def test_branch_encodings_match_reviewed_instructions(self):
        self.assertEqual(thumb_bl(0x22010596, 0x2200E23C).hex(), "fdf751fe")
        self.assertEqual(arm_b(0x22000000, 0x2201A730).hex(), "ca6900ea")
        for encode, source, target in (
            (thumb_bl, 0x1001, 0x1000),
            (thumb_bl, 0x1000, 0x1001),
            (thumb_bl, 0x1000, 0x401004),
            (thumb_bl, 0x400000, 2),
            (arm_b, 0x1002, 0x1000),
            (arm_b, 0x1000, 0x1002),
            (arm_b, 0x1000, 0x2001008),
            (arm_b, 0x2000000, 4),
        ):
            with (
                self.subTest(source=source, target=target),
                self.assertRaises(ValueError),
            ):
                encode(source, target)

    def test_hooks_resolve_thumb_symbols_and_preserve_source_checks(self):
        recipe = Recipe({"apple_loader": {"bytes": 32}})
        declarations = [
            Declaration(Kind.EXPECT, "apple_loader", 0, 4, 4, b"old!"),
            Declaration(Kind.THUMB_CALL, "apple_loader", 0x1101, 4, 4),
            Declaration(Kind.ARM_JUMP, "apple_loader", 0x1104, 8, 4),
            Declaration(Kind.FFS_CHECKSUM, "apple_loader", 0, 16, 1),
        ]
        copies, writes = apply(
            recipe,
            bytes(8),
            {"image_start": 0x1100},
            declarations,
            "apple_loader",
            0x1000,
        )
        self.assertEqual(copies, [])
        self.assertEqual(
            writes, [(4, bytes.fromhex("00f07cf8")), (8, bytes.fromhex("3d0000ea"))]
        )
        self.assertEqual(
            recipe.checks, [{"name": "apple_loader", "offset": 4, "hex": b"old!".hex()}]
        )
        self.assertEqual(recipe.ffs_checksums, [16])

    def test_hooks_reject_wrong_images_sizes_and_outside_targets(self):
        for declaration in (
            Declaration(Kind.THUMB_CALL, "rom", 0x1001, 0, 4),
            Declaration(Kind.THUMB_CALL, "apple_loader", 0x1001, 0, 2),
            Declaration(Kind.THUMB_CALL, "apple_loader", 0x1005, 0, 4),
            Declaration(Kind.THUMB_CALL, "apple_loader", 0x0FFF, 0, 4),
            Declaration(Kind.ARM_JUMP, "apple_loader", 0x1001, 0, 4),
            Declaration(Kind.FFS_CHECKSUM, "rom", 0, 0, 1),
        ):
            with self.subTest(declaration=declaration), self.assertRaises(ValueError):
                apply(
                    Recipe({}),
                    bytes(4),
                    {"image_start": 0x1000},
                    [declaration],
                    "apple_loader",
                )

    def test_preimage_and_copy_records_share_a_section(self):
        data = RECORD.pack(3, 0, 2, 3, b"ROM") + b"old\0"
        data += RECORD.pack(2, 0x1000, 5, 4, b"ROM")
        self.assertEqual(
            parse(data),
            [
                Declaration(3, "ROM", 0, 2, 3, b"old"),
                Declaration(2, "ROM", 0x1000, 5, 4),
            ],
        )

    def test_invalid_and_truncated_records_fail(self):
        for data in (
            b"short",
            RECORD.pack(7, 0, 0, 1, b"ROM"),
            RECORD.pack(2, 0, 0, 0, b"ROM"),
            RECORD.pack(2, 0, 0, 1, b"x" * 32),
            RECORD.pack(2, 0, 0, 1, b""),
            RECORD.pack(3, 0, 0, 3, b"ROM") + b"ab",
        ):
            with self.subTest(data=data), self.assertRaises(ValueError):
                parse(data)

    def test_linked_destinations_preserve_the_patch_between_copies(self):
        recipe = Recipe({"rom": {"bytes": 8}})
        code = bytes(8) + b"\0\0NEW!\0\0"
        declarations = [
            Declaration(1, "ROM", 0x2008, 2, 8),
            Declaration(2, "ROM", 0x2008, 0, 2),
            Declaration(3, "ROM", 0, 2, 4, b"old!"),
            Declaration(2, "ROM", 0x200E, 6, 2),
        ]
        recipe.overlay(
            code, apply(recipe, code, {"image_start": 0x2000}, declarations)[0]
        )
        self.assertEqual(recipe.relocations, {"rom": {"base": 0x2008, "count": 2}})
        self.assertEqual(
            recipe.checks, [{"name": "rom", "offset": 2, "hex": b"old!".hex()}]
        )
        self.assertIn(b"NEW!", recipe.data)
        self.assertEqual(
            [segment for segment in recipe.segments if segment["kind"] == "input"],
            [
                {"kind": "input", "name": "rom", "offset": 0, "bytes": 2},
                {"kind": "input", "name": "rom", "offset": 6, "bytes": 2},
            ],
        )

    def test_invalid_slots_and_source_sizes_fail(self):
        for declaration in (
            Declaration(1, "ROM", 0x1000, 2, 3),
            Declaration(2, "ROM", 0x0FFF, 0, 1),
            Declaration(2, "ROM", 0x1004, 0, 1),
            Declaration(2, "ROM", 0x1000, 0, 1),
            Declaration(2, "ROM", 0x1001, 4, 1),
        ):
            recipe = Recipe({"rom": {"bytes": 4}})
            code = b"X\0\0\0"
            with self.subTest(declaration=declaration), self.assertRaises(ValueError):
                recipe.overlay(
                    code, apply(recipe, code, {"image_start": 0x1000}, [declaration])[0]
                )
