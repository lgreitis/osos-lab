# SPDX-License-Identifier: GPL-3.0-only
"""Offline UI declaration contracts."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from patching import ui
from patching.native_ui import (
    LEGAL_ITEM,
    MAIN_SCREEN,
    MenuBuilder,
    Resources,
    pack_blocks,
)
from patching.ui_codegen import emit_bindings, emit_resources


def record(kind, name="", text="", ref="", numbers=()):
    numbers = [(n + (1 << 31)) % (1 << 32) - (1 << 31) for n in numbers]
    return struct.pack(
        "<6i64s96s64s",
        kind,
        *numbers,
        *([0] * (5 - len(numbers))),
        name.encode(),
        text.encode(),
        ref.encode(),
    )


def menu(rows):
    return (
        record(0, numbers=(1,))
        + record(1, "choices")
        + record(2, text="Off")
        + record(2, text="On", numbers=(1,))
        + record(4)
        + record(5, "TEST", "Test")
        + b"".join(rows)
        + record(6)
    )


class UiTests(unittest.TestCase):
    def test_text_pages_and_string_constants(self):
        data = (
            record(0, numbers=(1,))
            + record(10, "HELP", "Help", "help.txt")
            + record(9, "HELP", ref="", numbers=(LEGAL_ITEM,))
            + record(11, "CUSTOM_NAME", "Custom")
        )
        document = ui.parse(data)
        self.assertEqual(document.root.text_file, "help.txt")
        self.assertEqual(document.strings, {"CUSTOM_NAME": "Custom"})
        self.assertEqual(document.after, LEGAL_ITEM)
        self.assertIn(
            "#define CUSTOM_NAME 0xcf00000u",
            ui.header(document, {"CUSTOM_NAME": 0xCF00000}),
        )

    def test_storage_slots_survive_menu_reordering(self):
        first = record(7, "FIRST", "First", "choices", (0, 0))
        second = record(7, "SECOND", "Second", "choices", (1, 1))
        document = ui.parse(menu([second, first]))
        self.assertEqual([f.name for f in document.fields], ["FIRST", "SECOND"])
        self.assertEqual([f.name for f in document.root.items], ["SECOND", "FIRST"])
        header = ui.header(document)
        self.assertIn("FIRST = 0", header)
        self.assertIn("SECOND = 1", header)
        self.assertIn("TEST_FIELDS = 2", header)

    def test_invalid_declarations_fail_before_resource_generation(self):
        cases = [
            [record(7, "FIELD", "Field", "missing", (0, 0))],
            [record(7, "FIELD", "Field", "choices", (0, 2))],
            [
                record(7, "FIRST", "First", "choices", (0, 0)),
                record(7, "SECOND", "Second", "choices", (0, 0)),
            ],
            [record(7, "FIELD", "Field", "choices", (1, 0))],
            [
                record(7, "FIELD", "Field", "choices", (0, 0)),
                record(7, "FIELD", "Again", "choices", (1, 0)),
            ],
        ]
        for rows in cases:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                ui.parse(menu(rows))

    def test_decimal_labels_preserve_zero_and_sign(self):
        data = (
            record(0, numbers=(1,))
            + record(3, "gain_values", " dB", numbers=(-5, 5, 5, 10, 1))
            + record(5, "TEST", "Test")
            + record(7, "GAIN", "Gain", "gain_values", (0, 0))
            + record(6)
        )
        document = ui.parse(data)
        self.assertEqual(
            document.values["gain_values"].labels, ["-0.5 dB", "0.0 dB", "+0.5 dB"]
        )

    def test_truncated_and_unclosed_declarations_fail(self):
        for data in (b"bad", menu([])[:-248], menu([]) + record(6)):
            with self.subTest(data=data), self.assertRaises(ValueError):
                ui.parse(data)

    def test_invalid_ranges_fail(self):
        for numbers in (
            (0, 10, 0, 10, 0),
            (0, 10, -1, 10, 0),
            (0, 10, 1, 3, 0),
            (0, 100, 1, 10, 0),
        ):
            data = record(0, numbers=(1,)) + record(3, "values", numbers=numbers)
            with self.subTest(numbers=numbers), self.assertRaises(ValueError):
                ui.parse(data)

    def test_native_bindings_follow_rows_but_actions_keep_storage_slots(self):
        document = ui.parse(
            menu(
                [
                    record(8, "TEST_Save", "Save", "choices"),
                    record(7, "SECOND", "Second", "choices", (1, 1)),
                    record(7, "FIRST", "First", "choices", (0, 0)),
                ]
            )
        )
        resources = Resources.__new__(Resources)
        row = bytearray(0x98)
        struct.pack_into("<I", row, 0x30, LEGAL_ITEM)
        resources.original = {
            ("ITEM", 0x41): pack_blocks([(0, row)]),
            ("SCST", MAIN_SCREEN): b"screen",
            ("SLst", 0x0DAD09C3): pack_blocks([(0, bytes(4))]),
            ("SLyt", 0x0DAD09C4): pack_blocks([(0, struct.pack("<I", 10))]),
            ("SSin", 10): pack_blocks([]),
            ("SEVT", 0x0DAD09C4): b"events",
        }
        resources.added, resources.names, resources.next_id = {}, {}, 0x0CF00000
        builder = MenuBuilder(resources, document)
        builder.menu(document.root)
        self.assertEqual(builder.fields[0].parent_index, 2)
        self.assertEqual(builder.fields[1].parent_index, 1)
        self.assertEqual(builder.actions["TEST_Save"].index, 0)
        events = b"".join(
            bytes(data.data) if hasattr(data, "data") else data
            for (kind, _), data in resources.added.items()
            if kind == "CEVT"
        )
        self.assertIn(b"TEST_Set_00_00", events)
        self.assertIn(b"TEST_Set_01_01", events)
        builder.finish_bindings()
        next_id = resources.next_id
        added = list(resources.added)
        bindings = emit_bindings(document, builder.fields, builder.actions)
        emitted = emit_resources(resources)
        self.assertIn("test_save", "\n".join(bindings))
        self.assertEqual(
            bindings, emit_bindings(document, builder.fields, builder.actions)
        )
        self.assertEqual(emitted, emit_resources(resources))
        self.assertEqual(next_id, resources.next_id)
        self.assertEqual(added, list(resources.added))


if __name__ == "__main__":
    unittest.main()
