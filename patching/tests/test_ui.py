# SPDX-License-Identifier: GPL-3.0-only
"""Offline UI declaration contracts."""

import json
import re
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from patching.ui import generate, parser
from patching.ui.codegen import emit_bindings, emit_header
from patching.ui.compiler import compile_document
from patching.ui.model import ScreenEvent
from patching.ui.resources import (
    Resource,
    Resources,
    blocks,
    event,
    menu_event,
    pack_blocks,
    word,
)

LEGAL_ITEM = 100
MAIN_SCREEN = 101


def setting(name, slot, default=0, values="choices"):
    return (
        f'<setting id="{name}" title="{name}" values="{values}" '
        f'slot="{slot}" default="{default}"/>'
    )


def menu_rows(resources, table):
    stock = blocks(resources.stock("ITEM", table))
    additions = {}
    for target, after, kind, row in resources.menu_items:
        if target == table:
            index = next(
                i for i, (_, native) in enumerate(stock) if word(native, 0x30) == after
            )
            additions.setdefault(index, []).append((kind, row))
    return [
        row for i, native in enumerate(stock) for row in [native, *additions.get(i, [])]
    ]


def event_resource(resources, kind, resource_id):
    resource_id = resources.event_aliases.get((kind, resource_id), resource_id)
    return resources.added[kind, resource_id]


def canvas(settings):
    return (
        '<ui><values id="choices">'
        '<value number="0" label="Off"/><value number="1" label="On"/>'
        "</values>" + "".join(settings) + '<canvas id="TEST" title="Test"/></ui>'
    )


class UiTests(unittest.TestCase):
    def test_generated_actions_register_only_the_names_they_resolve(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        with tempfile.TemporaryDirectory() as directory:
            code = generate(
                resources,
                sorted((root / "payload").glob("*.ui")),
                Path(directory),
                "test",
            ).read_text()
        expected = {
            "CFW_Info_Screen",
            "CFW_Info_Layout",
            "CFW_EQ_Screen",
            "CFW_EQ_Layout",
            *[
                f"CFW_{name}_Preview"
                for name in (
                    "Info",
                    "EQ",
                    "DarkMode",
                )
            ],
        }
        self.assertEqual(resources.runtime_names, expected)
        table = code.split("cfw_resource_names[] = {", 1)[1].split("};", 1)[0]
        self.assertEqual(set(re.findall(r'"([^"]+)"', table)), expected)
        with self.assertRaisesRegex(ValueError, "Unknown runtime resource"):
            resources.runtime_name("missing")

    def test_runtime_list_uses_native_rows_and_returns_to_parent(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.parse('<ui><list id="CFW_List" title="Items"/></ui>')
        compiled = compile_document(resources, document)
        for suffix in ("Rows", "Detail", "Screen", "Layout"):
            self.assertIn("CFW_List_" + suffix, compiled.string_ids)
        rows = [
            row
            for (kind, _), data in resources.added.items()
            if kind == "ITEM"
            for _, row in blocks(data)
        ]
        self.assertEqual(len(rows), 1)  # One repeatable prototype, no row cap.
        self.assertEqual(rows[0].data[0x94], 1)
        source = word(rows[0], 0x68)
        bindings = blocks(resources.added["SORC", source])
        self.assertEqual(
            [bytes(binding.data) for _, binding in bindings],
            [
                struct.pack("<III", 0x8900, compiled.string_ids["CFW_List_Rows"], 1),
                struct.pack("<III", 0x8900, compiled.string_ids["CFW_List_Detail"], 10),
            ],
        )
        screen = compiled.string_ids["CFW_List_Screen"]
        events = bytes(resources.added["CEVT", screen].data)
        self.assertIn(b"CFW_List_Select", events)
        self.assertIn(b"navigator.PopTopScreen", events)
        self.assertFalse(document.fields)  # Runtime rows are not preference slots.

    def test_runtime_list_can_stay_open_when_a_row_is_chosen(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.parse(
            '<ui><list id="CFW_List" title="Items" select="stay"/></ui>'
        )
        compiled = compile_document(resources, document)
        events = bytes(
            resources.added["CEVT", compiled.string_ids["CFW_List_Screen"]].data
        )
        self.assertIn(b"CFW_List_Select", events)
        self.assertNotIn(b"navigator.PopTopScreen", events)
        with self.assertRaisesRegex(ValueError, "select must be"):
            parser.parse('<ui><list id="CFW_List" title="Items" select="push"/></ui>')

    def test_runtime_list_can_stack_the_detail_below_the_row_text(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        for layout, style in (("inline", None), ("stacked", 6)):
            with self.subTest(layout=layout):
                resources = Resources(metadata["resources"], bindings)
                document = parser.parse(
                    f'<ui><list id="CFW_List" title="Items" layout="{layout}"/></ui>'
                )
                compile_document(resources, document)
                [row] = [
                    row
                    for (kind, _), data in resources.added.items()
                    if kind == "ITEM"
                    for _, row in blocks(data)
                ]
                if style is not None:
                    self.assertEqual(word(row, 0x60), style)
                else:
                    self.assertIsNotNone(row.origins[0x60])  # Native row style kept.
        with self.assertRaisesRegex(ValueError, "layout must be"):
            parser.parse('<ui><list id="CFW_List" title="Items" layout="grid"/></ui>')

    def test_sound_check_template_is_required_for_lists_and_toggles(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        for kind in ("list", "toggle"):
            with self.subTest(kind=kind):
                resources = Resources(metadata["resources"], bindings)
                table = bindings["SettingsMenu_Items"]
                rows = [
                    next(
                        (row_kind, row)
                        for row_kind, row in blocks(resources.stock("ITEM", table))
                        if word(row, 0x30) == bindings["SettingsMenu_ListItem_Legal"]
                    )
                ]
                resources.original["ITEM", table] = pack_blocks(rows)
                document = parser.parse(
                    f'<ui><{kind} id="TEST" title="Test"/>'
                    '<settings-entry menu="TEST" after="SettingsMenu_ListItem_Brightness" preview="image"/>'
                    "</ui>"
                )
                with self.assertRaisesRegex(ValueError, "missing the Sound Check row"):
                    compile_document(resources, document)

    def test_runtime_list_can_be_opened_from_settings(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.parse(
            '<ui><list id="TEST" title="Test"/>'
            '<settings-entry menu="TEST" after="SettingsMenu_ListItem_Brightness" preview="image"/>'
            "</ui>"
        )
        compiled = compile_document(resources, document)
        [(table, after, _, row)] = resources.menu_items
        self.assertEqual(table, bindings["SettingsMenu_Items"])
        self.assertEqual(after, bindings["SettingsMenu_ListItem_Brightness"])
        events = b"".join(
            resources.event_extensions["CEVT", bindings["SettingsMenus_Main_Screen"]]
        )
        self.assertIn(f"list.pid.{word(row, 0x30)}.chosen".encode(), events)
        self.assertIn(b"navigator.PushScreen", events)
        self.assertIn(b"TEST_Screen", events)
        self.assertIn("TEST_Rows", compiled.string_ids)
        self.assertFalse(document.fields)

    def test_canvas_has_fixed_extents_without_scroll_or_brightness_bindings(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        compile_document(
            resources,
            parser.parse('<ui><canvas id="CFW_EQCanvas" title="Graphic EQ"/></ui>'),
        )
        screen = resources.names["CFW_EQCanvas_Screen"]
        self.assertEqual(resources.added["SCST", screen].data, b"CFWCanvas")
        [(template, views)] = [
            (resource_id, data)
            for (kind, resource_id), data in resources.added.items()
            if kind == "View"
        ]
        [(kind, view)] = blocks(views)
        self.assertEqual((kind, word(view, 0x4C)), (0x63667763, 0x63667763))
        # Overlap the header divider; extents remain independent of parent content.
        self.assertEqual(
            struct.unpack("<12i", view[:0x30].data),
            (0, 0, 0, 23, 0, 0, 217, 0, 6, 320, 0, 5),
        )
        layout = resources.names["CFW_EQCanvas_Layout"]
        instances = [
            instance
            for _, group in blocks(resources.added["SLyt", layout])
            for _, instance in blocks(resources.current("SSin", word(group, 0)))
        ]
        canvas = next(
            instance for instance in instances if word(instance, 4) == template
        )
        self.assertEqual(word(canvas, 0x10) >> 16 & 0xFF, 0)
        self.assertEqual(blocks(resources.current("VCvs", word(canvas, 0))), [])
        events = event_resource(resources, "SEVT", layout)
        self.assertEqual(
            events.data,
            struct.pack("<I", 2)
            + event("button.menu.up", "navigator.PopTopScreen")
            + event("button.menu.pressandhold", "navigator.PopToMainScreen"),
        )

    def test_custom_eq_settings_open_the_canvas_directly(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.read(root / "payload/custom_eq.ui")
        resources.allocate("CFW_EQ_Icon")
        compile_document(resources, document)
        self.assertEqual(document.root.kind, "canvas")
        self.assertEqual(document.root.title, "Custom EQ")
        self.assertEqual([item.slot for item in document.fields], list(range(21)))
        self.assertEqual(
            sum(len(document.values[item.choices].labels) for item in document.fields),
            717,
        )
        screen = resources.names["CFW_EQ_Screen"]
        self.assertEqual(resources.added["SCST", screen].data, b"CFWCanvas")
        # Settings gains one row, which pushes the canvas without an open action.
        [(_, _, _, row)] = resources.menu_items
        events = resources.event_extensions[
            "CEVT", bindings["SettingsMenus_Main_Screen"]
        ]
        self.assertIn(
            menu_event(
                word(row, 0x30),
                "chosen",
                "",
                "navigator.PushScreen",
                ["CFW_EQ_Screen", "CFW_EQ_Layout", "foregroundpush"],
            ),
            events,
        )
        self.assertFalse(
            [
                key
                for key in resources.added
                if key[0] == "ITEM" and key not in resources.original
            ]
        )
        self.assertEqual(sum(kind == "View" for kind, _ in resources.added), 1)
        lines = "\n".join(emit_bindings(document))
        self.assertIn('{102, 0, "Precut", cfw_eq_precut_values_labels},', lines)
        self.assertIn('{4, 2, "Type", cfw_eq_types_labels},', lines)
        precut = document.values["cfw_eq_precut_values"]
        self.assertEqual(precut.numbers, [1, *range(0, -101, -1)])
        self.assertEqual(precut.labels[:3], ["Auto", "0.0 dB", "-0.1 dB"])
        self.assertEqual(document.fields[0].default_index, 0)

    def test_layout_reuses_unchanged_instance_resources(self):
        resources = Resources(
            [], {"StatusBarWhite_Template": 100, "StatusBarWhite_Text": 101}
        )
        title = pack_blocks([(101, pack_blocks([(0x534F5243, struct.pack("<I", 0))]))])
        untouched = pack_blocks([(200, bytes(8))])
        resources.original = {
            ("SLyt", 1): pack_blocks([(0, struct.pack("<I", 2))]),
            ("SSin", 2): pack_blocks(
                [
                    (0, struct.pack("<6I", 10, 100, 100, 0, 0, 0)),
                    (0, struct.pack("<6I", 11, 200, 200, 0, 0, 0)),
                ]
            ),
            ("VCvs", 10): title,
            ("VCvs", 11): untouched,
            ("VCrv", 10): pack_blocks([(0, bytes(48))]),
            ("VCrv", 11): pack_blocks([(0, bytes(48))]),
            ("SEVT", 1): pack_blocks([]),
        }
        layout = resources.allocate()
        resources.clone_layout(1, layout, 99, {200: 201}, item_table=55)
        group = word(blocks(resources.added["SLyt", layout])[0][1], 0)
        first, second = [row for _, row in blocks(resources.added["SSin", group])]
        changed = word(first, 0)
        self.assertNotEqual(changed, 10)
        self.assertEqual(word(blocks(resources.added["VCvs", changed])[0][1], 12), 99)
        self.assertEqual(word(second, 0), 11)
        self.assertEqual(word(second, 4), 201)
        self.assertEqual(word(second, 8), 201)
        self.assertEqual(sum(kind == "VCvs" for kind, _ in resources.added), 1)
        self.assertEqual(sum(kind == "VCrv" for kind, _ in resources.added), 1)
        self.assertEqual(word(blocks(title)[0][1], 12), 0)

    def test_cfw_info_preview_uses_static_bitmap_and_caption(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        with tempfile.TemporaryDirectory() as directory:
            generate(resources, [root / "payload/cfw_info.ui"], Path(directory), "test")
        layout = resources.added["SLyt", resources.names["CFW_Info_Preview"]]
        screen = resources.added["SSin", word(blocks(layout)[0][1], 0)]
        preview_id = next(
            word(data, 8) for _, data in blocks(screen) if word(data, 4) == 0x0DAD0C2E
        )
        views = dict(blocks(resources.added["VLyt", preview_id]))
        for view, kind in (
            ("Settings_Icon_View", "BMap"),
            ("Settings_InfoDetail_View", "Str "),
        ):
            options = dict(blocks(views[bindings[view]]))
            source = resources.added["SORC", word(options[0x534F5243], 0)]
            binding = blocks(source)[0][1]
            self.assertEqual(
                (word(binding, 0), word(binding, 8)),
                (0x8900 if kind == "BMap" else 0x80, 1),
            )
            resource_id = word(binding, 4)
            content = resources.added[kind, resource_id].data
            if kind == "BMap":
                self.assertEqual(resource_id, resources.names["CFW_Info_Icon"])
                self.assertEqual(
                    struct.unpack_from("<HHHHiiiii", content),
                    (8, 0, 80, 8, 0, 0, 121, 78, 9680),
                )
            else:
                self.assertEqual(content, b"RepriseOS\0")
        appended = b"".join(
            resources.event_extensions["CEVT", bindings["SettingsMenus_Main_Screen"]]
        )
        self.assertNotIn(
            ("CEVT", bindings["SettingsMenus_Main_Screen"]), resources.added
        )
        self.assertIn(b"CFW_Info_Screen", appended)
        self.assertIn(b"navigator.PushScreen", appended)
        self.assertNotIn(b"ShowSetting_Legal", appended)

    def test_settings_toggle_preserves_stock_rows_and_has_no_navigation(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.read(root / "payload/dark_mode.ui")
        compiled = compile_document(resources, document)
        stock = blocks(resources.stock("ITEM", "SettingsMenu_Items"))
        rows = menu_rows(resources, bindings["SettingsMenu_Items"])
        self.assertNotIn(("ITEM", bindings["SettingsMenu_Items"]), resources.added)
        # Pinned 2.0.5 Brightness ID; 0x0dad0c71 is Backlight Timer.
        anchor = next(
            i for i, (_, row) in enumerate(rows) if word(row, 0x30) == 0x0DAD0C72
        )
        _, toggle = rows.pop(anchor + 1)
        for (_, before), (_, after) in zip(stock, rows, strict=True):
            self.assertEqual(before.data, after.data)
            self.assertEqual(before.origins, after.origins)
        prototype = next(
            row
            for _, row in stock
            if word(row, 0x30) == bindings["SettingsMenu_ListItem_SoundCheck"]
        )
        for start, end in ((0, 0x30), (0x34, 0x68), (0x6C, 0x98)):
            self.assertEqual(toggle[start:end].origins, prototype[start:end].origins)
            self.assertEqual(toggle[start:end].data, prototype[start:end].data)
        sources = blocks(resources.added["SORC", word(toggle, 0x68)])
        self.assertEqual(
            [word(sources[1][1], offset) for offset in (0, 4, 8)],
            [0x8900, compiled.string_ids["CFW_DarkMode_Value"], 10],
        )
        appended = b"".join(
            resources.event_extensions["CEVT", bindings["SettingsMenus_Main_Screen"]]
        )
        self.assertNotIn(
            ("CEVT", bindings["SettingsMenus_Main_Screen"]), resources.added
        )
        self.assertIn(b"CFW_DarkMode_Toggle", appended)
        self.assertNotIn(b"navigator.PushScreen", appended)
        self.assertNotIn(b"ShowSetting_Legal", appended)
        self.assertNotIn("CFW_DarkMode_Screen", resources.names)
        preview = next(
            data for (kind, _), data in resources.added.items() if kind == "VLyt"
        )
        views = dict(blocks(preview))
        stock_views = dict(
            blocks(resources.stock("VLyt", "SettingsInfo_Template_Brightness_Layout"))
        )
        for view, suffix in (
            ("Settings_Icon_View", "Image"),
            ("Settings_InfoDetail_View", "Text"),
        ):
            view_id = bindings[view]
            options = dict(blocks(views[view_id]))
            source = word(options[0x534F5243], 0)
            binding = blocks(resources.added["SORC", source])[0][1]
            self.assertEqual(
                [word(binding, offset) for offset in (0, 4, 8)],
                [0x8900, compiled.string_ids["CFW_DarkMode_Preview" + suffix], 1],
            )
            for kind, original in blocks(stock_views[view_id]):
                if kind != 0x534F5243:
                    self.assertEqual(options[kind].data, original.data)
                    self.assertEqual(options[kind].origins, original.origins)
        base_panel = 0x0DAD0C2F
        self.assertEqual(views[base_panel].data, stock_views[base_panel].data)
        self.assertEqual(views[base_panel].origins, stock_views[base_panel].origins)

    def test_media_menus_bind_select_hold_on_movie_and_tv_lists(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.read(root / "payload/media_menus.ui")
        compile_document(resources, document)
        self.assertEqual(
            [item.screen for item in document.events],
            ["MediaLists_Movies_Layout", "MediaLists_TVEpisodes_Layout"],
        )
        for item_event in document.events:
            events = b"".join(
                resources.event_extensions[
                    item_event.kind, resources.native(item_event.screen)
                ]
            )
            self.assertIn(b"button.select.pressandhold", events)
            self.assertIn(b"HandleShowContextualMenu", events)

    def test_layout_event_extends_the_native_layout_events(self):
        document = parser.parse(
            '<ui><layout-event layout="0x20" event="button.select.pressandhold" '
            'handler="HandleShowContextualMenu"/></ui>'
        )
        resources = Resources([], {})
        resources.original = {("SEVT", 0x20): Resource(struct.pack("<I", 0))}
        compile_document(resources, document)
        self.assertEqual(
            resources.event_extensions["SEVT", 0x20],
            [event("button.select.pressandhold", handler="HandleShowContextualMenu")],
        )
        with self.assertRaisesRegex(ValueError, "Layout events require"):
            parser.parse('<ui><layout-event layout="0x20" event="" handler="X"/></ui>')

    def test_named_screen_uses_selected_binding_and_rejects_missing_names(self):
        document = parser.parse(
            '<ui><screen-event screen="Songs" event="select" handler="Select"/></ui>'
        )
        resources = Resources([], {"Songs": 10})
        resources.original = {
            ("SLst", 10): pack_blocks([(0, struct.pack("<I", 20))]),
            ("SEVT", 20): struct.pack("<I", 0),
            ("CEVT", 10): struct.pack("<I", 0),
        }
        resources.bind_screen_event(document.events[0])
        self.assertEqual(len(resources.event_extensions["CEVT", 10]), 1)
        resources.bindings.clear()
        with self.assertRaises(KeyError):
            resources.bind_screen_event(document.events[0])

    def test_screen_events_are_one_extension_for_all_layouts(self):
        document = parser.parse(
            '<ui><screen-event screen="10" event="contextualMenu.CFW_Example" '
            'handler="CFW_Example"/></ui>'
        )
        stock = struct.pack("<I", 1) + event("button.menu.up", "navigator.PopTopScreen")
        resources = Resources([])
        resources.original = {
            ("SLst", 10): pack_blocks(
                [(0, struct.pack("<I", 20)), (0, struct.pack("<I", 21))]
            ),
            ("SEVT", 20): stock,
            ("SEVT", 21): stock,
            ("CEVT", 10): struct.pack("<I", 0),
        }
        resources.bind_screen_event(document.events[0])
        resources.bind_screen_event(ScreenEvent(10, "test.other", "OtherHandler"))
        data = b"".join(resources.event_extensions["CEVT", 10])
        self.assertIn(b"contextualMenu.CFW_Example1", data)
        self.assertIn(b"CFW_Example\0\0\0\0", data)
        self.assertIn(b"test.other1", data)
        self.assertFalse(resources.added)
        for layout in (20, 21):
            self.assertEqual(resources.original["SEVT", layout], stock)

    def test_storage_slots_survive_declaration_reordering(self):
        first = setting("FIRST", 0)
        second = setting("SECOND", 1, 1)
        document = parser.parse(canvas([second, first]))
        self.assertEqual([f.name for f in document.fields], ["FIRST", "SECOND"])
        self.assertEqual(document.fields[1].default_index, 1)
        header = emit_header(document)
        self.assertIn("FIRST = 0", header)
        self.assertIn("SECOND = 1", header)
        self.assertIn("TEST_FIELDS = 2", header)

    def test_invalid_declarations_fail_before_resource_generation(self):
        cases = [
            [setting("FIELD", 0, values="missing")],
            [setting("FIELD", 0, 2)],
            [setting("FIRST", 0), setting("SECOND", 0)],
            [setting("FIELD", 1)],
            [setting("FIELD", 0), setting("FIELD", 1)],
            [setting("FIELD", -1)],
            [setting("FIELD", 100)],
            [setting("bad-name", 0)],
            ['<setting id="FIELD" title="Field" values="choices" slot="0"/>'],
        ]
        for settings in cases:
            with self.subTest(settings=settings), self.assertRaises(ValueError):
                parser.parse(canvas(settings))
        for root in (
            '<string id="NAME" text="Text"/>',
            '<list id="LIST" title="List"/>',
        ):
            with self.subTest(root=root), self.assertRaises(ValueError):
                parser.parse(
                    '<ui><values id="choices"><value number="0" label="Off"/></values>'
                    + setting("FIELD", 0)
                    + root
                    + "</ui>"
                )

    def test_malformed_or_unknown_xml_fails(self):
        for data in (
            "bad",
            "<ui><text></ui>",
            '<ui><menu id="MENU" title="Menu"><info id="INFO" title="Info"/></menu></ui>',
            '<ui><info id="INFO" title="Info"/></ui>',
            "<ui/>",
            "<other/>",
            '<ui><string id="NAME" text="Text" typo="1"/></ui>',
            '<ui><string id="NAME"/></ui>',
            "<ui><unknown/></ui>",
            '<ui><string id="NAME" text="Text">unexpected</string></ui>',
            '<ui><string id="NAME" text="Text"/>unexpected</ui>',
            '<ui><text id="PAGE" title="Page" file="page.txt" body="bad-name"/></ui>',
            '<ui><text id="PAGE" title="Page" file="page.txt" body="PAGE"/></ui>',
            '<ui><text id="PAGE" title="Page" file="page.txt"/>'
            '<settings-entry menu="OTHER" after="1"/></ui>',
        ):
            with self.subTest(data=data), self.assertRaises(ValueError):
                parser.parse(data)

    def test_invalid_ranges_fail(self):
        for numbers in (
            (0, 10, 0, 10, 0),
            (0, 10, -1, 10, 0),
            (0, 10, 1, 3, 0),
            (0, 1024, 1, 10, 0),
        ):
            first, last, step, scale, _ = numbers
            data = (
                f'<ui><range id="values" first="{first}" last="{last}" '
                f'step="{step}" scale="{scale}"/>'
                '<string id="NAME" text="Text"/></ui>'
            )
            with self.subTest(numbers=numbers), self.assertRaises(ValueError):
                parser.parse(data)

    def test_range_named_choices_count_and_stay_unique(self):
        for data in (
            '<range id="values" first="0" last="1023" step="1">'
            '<value number="-1" label="Auto"/></range>',
            '<range id="values" first="0" last="10" step="1">'
            '<value number="0" label="Auto"/></range>',
        ):
            with self.subTest(data=data), self.assertRaises(ValueError):
                parser.parse(f'<ui>{data}<string id="NAME" text="Text"/></ui>')

    def test_ranges_keep_scaled_labels_and_default_indices(self):
        document = parser.parse("""<ui>
          <range id="gain_values" first="-5" last="5" step="5"
                 scale="10" suffix=" dB" signed="true"/>
          <range id="precut" first="0" last="-10" step="-5" scale="10">
            <value number="1" label="Auto"/>
          </range>
          <setting id="GAIN" title="Gain" values="gain_values" slot="0" default="0"/>
          <canvas id="EQ" title="EQ"/>
        </ui>""")
        self.assertEqual(
            document.values["gain_values"].labels, ["-0.5 dB", "0.0 dB", "+0.5 dB"]
        )
        self.assertEqual(document.values["precut"].numbers, [1, 0, -5, -10])
        self.assertEqual(
            document.values["precut"].labels, ["Auto", "0.0", "-0.5", "-1.0"]
        )
        self.assertEqual(document.fields[0].default_index, 1)

    def test_xml_text_and_hex_resource_ids(self):
        document = parser.parse("""<ui>
          <string id="LABEL" text="Music &amp; café &quot;mix&quot;"/>
          <screen-event screen="0x0DAD07B5" event="select" handler="Select"/>
        </ui>""")
        self.assertEqual(document.strings["LABEL"], 'Music & café "mix"')
        self.assertEqual(document.events[0].screen, 0x0DAD07B5)


if __name__ == "__main__":
    unittest.main()
