# SPDX-License-Identifier: GPL-3.0-only
"""Offline UI declaration contracts."""

import json
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from patching.ui import parser
from patching.ui.codegen import emit_header
from patching.ui.compiler import DocumentCompiler, compile_document
from patching.ui.model import ScreenEvent
from patching.ui.resources import (
    Resources,
    blocks,
    event,
    pack_blocks,
    word,
)

LEGAL_ITEM = 100
MAIN_SCREEN = 101


def selector(name, slot, default=0, values="choices"):
    return (
        f'<selector id="{name}" title="{name}" values="{values}" '
        f'slot="{slot}" default="{default}"/>'
    )


def menu(rows):
    return (
        '<ui><values id="choices">'
        '<value number="0" label="Off"/><value number="1" label="On"/>'
        '</values><menu id="TEST" title="Test">' + "".join(rows) + "</menu></ui>"
    )


class UiTests(unittest.TestCase):
    def test_settings_toggle_preserves_stock_rows_and_has_no_navigation(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.read(root / "payload/dark_mode.ui")
        compiled = compile_document(resources, document)
        stock = blocks(resources.stock("ITEM", "SettingsMenu_Items"))
        rows = blocks(resources.added["ITEM", bindings["SettingsMenu_Items"]])
        anchor = next(
            i
            for i, (_, row) in enumerate(rows)
            if word(row, 0x30) == bindings["SettingsMenu_ListItem_Brightness"]
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
        events = resources.added["CEVT", bindings["SettingsMenus_Main_Screen"]]
        original = resources.stock("CEVT", "SettingsMenus_Main_Screen")
        appended = bytes(events.data[len(original) :])
        self.assertIn(b"CFW_DarkMode_Toggle", appended)
        self.assertNotIn(b"navigator.PushScreen", appended)
        self.assertNotIn("CFW_DarkMode_Screen", resources.names)

    def test_album_artists_reuses_native_routes_and_adds_a_music_row(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        shim = root / "payload/native"
        bindings = json.loads((shim / "ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.read(root / "payload/album_artists.ui")
        compiled = compile_document(resources, document)
        rows = blocks(resources.added["ITEM", bindings["MusicMenu_Items"]])
        self.assertEqual(len(rows), 13)
        anchor = next(
            i
            for i, (_, row) in enumerate(rows)
            if word(row, 0x30) == bindings["MusicMenu_ListItem_Artists"]
        )
        new_row = rows[anchor + 1][1]
        item = word(new_row, 0x30)
        self.assertNotEqual(item, bindings["MusicMenu_ListItem_Artists"])
        self.assertEqual(word(new_row, 0x60), 5)
        self.assertEqual(word(new_row, 0x64) & 0xFF, 1)
        events = resources.added["CEVT", bindings["MainMenus_Music_Screen"]]
        self.assertIn(f"list.pid.{item}.chosen".encode(), events.data)
        self.assertIn(b"CFW_AlbumArtists", events.data)
        self.assertIn(b"HandleMusicHilited", events.data)
        main_rows = blocks(resources.added["ITEM", bindings["MainMenu_Items"]])
        main_anchor = next(
            i
            for i, (_, row) in enumerate(main_rows)
            if word(row, 0x30) == bindings["MainMenu_List_Artists"]
        )
        main_row = main_rows[main_anchor + 1][1]
        main_item = compiled.string_ids["CFW_AlbumArtists_MainItem"]
        self.assertEqual(word(main_row, 0x30), main_item)
        self.assertNotEqual(main_item, item)
        self.assertEqual(word(main_row, 0x64) & 0xFF, 0)
        self.assertEqual(word(main_row, 0x68), word(new_row, 0x68))
        main_events = resources.added["CEVT", bindings["MainMenus_Main_Screen"]]
        self.assertIn(f"list.pid.{main_item}.chosen".encode(), main_events.data)
        self.assertIn(b"CFW_AlbumArtists", main_events.data)
        for table, inserted in (
            ("MainMenu_Items", main_anchor + 1),
            ("MusicMenu_Items", anchor + 1),
        ):
            stock = blocks(resources.stock("ITEM", table))
            updated = blocks(resources.added["ITEM", bindings[table]])
            del updated[inserted]
            self.assertEqual(len(stock), len(updated))
            for (_, before), (_, after) in zip(stock, updated):
                self.assertEqual(before.data, after.data)
                self.assertEqual(before.origins, after.origins)
        # CEVT belongs to the screen and applies to every preview layout.
        screen = compiled.string_ids["CFW_AlbumArtists_Screen"]
        layout = compiled.string_ids["CFW_AlbumArtists_Layout"]
        native_layout = resources.stock("SLst", "MediaLists_Artists_Screen")
        self.assertEqual(len(blocks(native_layout)), 1)
        self.assertEqual(word(blocks(resources.added["SLst", screen])[0][1], 0), layout)
        for kind, new_id, original in (
            ("SCST", screen, "MediaLists_Artists_Screen"),
            ("CEVT", screen, "MediaLists_Artists_Screen"),
            ("SEVT", layout, "MediaLists_Artists_Screen_Default"),
        ):
            self.assertEqual(
                resources.added[kind, new_id].origins,
                resources.stock(kind, original).origins,
            )
        header = emit_header(document, compiled.string_ids)
        self.assertIn("#define CFW_AlbumArtists_Screen ", header)

    def test_main_entry_adds_a_hidden_row_with_a_live_preview(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.parse(
            '<ui><main-entry id="CFW_UpNext" title="Up Next" after="MainMenu_List_NowPlaying" '
            'open="CFW_UpNext" hilite="CFW_UpNextHilited" '
            'preview="MainMenu_Preview_Compilations" message="0 Songs"/></ui>'
        )
        compiled = compile_document(resources, document)
        item = compiled.string_ids["CFW_UpNext_MainItem"]
        rows = blocks(resources.added["ITEM", bindings["MainMenu_Items"]])
        anchor = next(
            i
            for i, (_, row) in enumerate(rows)
            if word(row, 0x30) == bindings["MainMenu_List_NowPlaying"]
        )
        row = rows[anchor + 1][1]
        self.assertEqual(word(row, 0x30), item)
        self.assertEqual(word(row, 0x64), 0)
        self.assertEqual(word(row, 0x5C) & 0xFFFF, 4)
        title = blocks(resources.added["SORC", word(row, 0x68)])
        self.assertEqual(
            word(title[0][1], 4), compiled.string_ids["CFW_UpNext_Title"]
        )
        stock = blocks(resources.stock("ITEM", "MainMenu_Items"))
        del rows[anchor + 1]
        self.assertEqual(len(stock), len(rows))
        for (_, before), (_, after) in zip(stock, rows):
            self.assertEqual(before.data, after.data)

        # The first preview line reads the <id>_Count property; the others are empty.
        preview = compiled.string_ids["CFW_UpNext_Preview"]
        layout = blocks(resources.added["SLyt", preview])
        sources = {
            view: word(binding, len(binding) - 4)
            for key, data in resources.added.items()
            if key[0] == "VCvs"
            for view, binding in blocks(data)
            if view
            in (
                bindings["PreviewArea_Watermark_Message"],
                bindings["PreviewArea_Watermark_Message2"],
                bindings["PreviewArea_Watermark_Message3"],
            )
            and key[1] >= 0x0CF00000
        }
        self.assertEqual(len(layout), 1)
        message = blocks(resources.added["SORC", sources[bindings["PreviewArea_Watermark_Message"]]])
        self.assertEqual(
            [word(message[0][1], offset) for offset in (0, 4)],
            [0x8900, compiled.string_ids["CFW_UpNext_Count"]],
        )
        self.assertEqual(
            sources[bindings["PreviewArea_Watermark_Message2"]],
            sources[bindings["PreviewArea_Watermark_Message3"]],
        )
        self.assertEqual(
            bytes(resources.added["Str ", compiled.string_ids["CFW_UpNext_Message"]].data),
            b"0 Songs\0",
        )

        screen = bindings["MainMenus_Main_Screen"]
        layouts = blocks(resources.added["SLst", screen])
        self.assertEqual(len(layouts), len(blocks(resources.stock("SLst", screen))) + 1)
        self.assertEqual(word(layouts[-1][1], 0), preview)
        events = resources.added["CEVT", screen]
        appended = bytes(events.data[len(resources.stock("CEVT", screen)) :])
        self.assertIn(f"list.pid.{item}.chosen".encode(), appended)
        self.assertIn(b"CFW_UpNext\0", appended)
        self.assertIn(f"list.pid.{item}.delayedselected".encode(), appended)
        self.assertIn(b"CFW_UpNextHilited", appended)
        self.assertIn(b"navigator.SwitchLayout", appended)
        self.assertIn(b"CFW_UpNext_Preview", appended)
        header = emit_header(document, compiled.string_ids)
        for name in ("MainItem", "Count", "Message", "Preview", "Title"):
            self.assertIn(f"#define CFW_UpNext_{name} ", header)

    def test_main_entry_rejects_missing_anchor_and_bad_declarations(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        entry = (
            '<main-entry id="X" title="X" after="{after}" open="O" hilite="H" '
            'preview="MainMenu_Preview_Compilations"/>'
        )
        document = parser.parse("<ui>" + entry.format(after="999") + "</ui>")
        with self.assertRaises(ValueError):
            compile_document(Resources(metadata["resources"], bindings), document)
        for xml in (
            '<ui><main-entry id="X" title="X" after="1" open="O"/></ui>',
            "<ui>" + entry.format(after="1") * 2 + "</ui>",
            "<ui>" + entry.format(after="1").replace("/>", ' extra="1"/>') + "</ui>",
        ):
            with self.assertRaises(ValueError):
                parser.parse(xml)

    def test_song_info_rows_are_dynamic_text_without_chosen_actions(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        shim = root / "payload/native"
        bindings = json.loads((shim / "ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.read(root / "payload/song_info.ui")
        compiled = compile_document(resources, document)
        screen = compiled.string_ids["CFW_SongInfo_Screen"]
        self.assertEqual(word(resources.added["CEVT", screen], 0), 0)
        rows = [
            row
            for (kind, _), data in resources.added.items()
            if kind == "ITEM"
            for _, row in blocks(data)
        ]
        self.assertEqual(len(rows), 16)
        self.assertTrue(all(word(row, 0x60) == 2 for row in rows))
        for item in document.root.items:
            string_id = compiled.string_ids[item.name]
            self.assertEqual(
                bytes(resources.added["Str ", string_id].data),
                (item.title + ": Unknown\0").encode(),
            )
        for item_event in document.events:
            layouts = blocks(resources.stock("SLst", item_event.screen))
            self.assertTrue(layouts)
            for _, layout in layouts:
                self.assertIn(
                    b"contextualMenu.CFW_SongInfo",
                    resources.added["SEVT", word(layout, 0)].data,
                )
        header = emit_header(document, compiled.string_ids)
        self.assertIn("#define CFW_SongInfo_Screen ", header)
        self.assertIn("#define CFW_SongInfo_Layout ", header)

    def test_named_screen_uses_selected_binding_and_rejects_missing_names(self):
        document = parser.parse(
            '<ui><screen-event screen="Songs" event="select" handler="Select"/></ui>'
        )
        resources = Resources([], {"Songs": 10})
        resources.original = {
            ("SLst", 10): pack_blocks([(0, struct.pack("<I", 20))]),
            ("SEVT", 20): struct.pack("<I", 0),
        }
        resources.bind_screen_event(document.events[0])
        self.assertEqual(resources.added["SEVT", 20].word(0), 1)
        resources.bindings.clear()
        with self.assertRaises(KeyError):
            resources.bind_screen_event(document.events[0])

    def test_screen_events_extend_each_layout_without_replacing_stock_events(self):
        document = parser.parse(
            '<ui><screen-event screen="10" event="contextualMenu.CFW_PlayNext" '
            'handler="CFW_PlayNext"/></ui>'
        )
        stock = struct.pack("<I", 1) + event("button.menu.up", "navigator.PopTopScreen")
        resources = Resources([])
        resources.original = {
            ("SLst", 10): pack_blocks(
                [(0, struct.pack("<I", 20)), (0, struct.pack("<I", 21))]
            ),
            ("SEVT", 20): stock,
            ("SEVT", 21): stock,
        }
        resources.bind_screen_event(document.events[0])
        resources.bind_screen_event(ScreenEvent(10, "test.other", "OtherHandler"))
        for layout in (20, 21):
            data = resources.added["SEVT", layout]
            self.assertEqual(data.word(0), 3)
            self.assertEqual(data.data[4 : len(stock)], stock[4:])
            self.assertIn(b"contextualMenu.CFW_PlayNext1", data.data)
            self.assertIn(b"CFW_PlayNext\0\0\0\0", data.data)
            self.assertIn(b"test.other1", data.data)
            self.assertEqual(resources.original["SEVT", layout], stock)

    def test_storage_slots_survive_menu_reordering(self):
        first = selector("FIRST", 0)
        second = selector("SECOND", 1, 1)
        document = parser.parse(menu([second, first]))
        self.assertEqual([f.name for f in document.fields], ["FIRST", "SECOND"])
        self.assertEqual([f.name for f in document.root.items], ["SECOND", "FIRST"])
        header = emit_header(document)
        self.assertIn("FIRST = 0", header)
        self.assertIn("SECOND = 1", header)
        self.assertIn("TEST_FIELDS = 2", header)

    def test_invalid_declarations_fail_before_resource_generation(self):
        cases = [
            [selector("FIELD", 0, values="missing")],
            [selector("FIELD", 0, 2)],
            [selector("FIRST", 0), selector("SECOND", 0)],
            [selector("FIELD", 1)],
            [selector("FIELD", 0), selector("FIELD", 1)],
            [selector("FIELD", -1)],
            [selector("FIELD", 100)],
            [selector("bad-name", 0)],
        ]
        for rows in cases:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                parser.parse(menu(rows))

    def test_malformed_or_unknown_xml_fails(self):
        for data in (
            "bad",
            "<ui><menu></ui>",
            menu([]),
            "<ui/>",
            "<other/>",
            '<ui><string id="NAME" text="Text" typo="1"/></ui>',
            '<ui><string id="NAME"/></ui>',
            "<ui><unknown/></ui>",
            '<ui><string id="NAME" text="Text">unexpected</string></ui>',
            '<ui><string id="NAME" text="Text"/>unexpected</ui>',
            '<ui><text id="PAGE" title="Page" file="page.txt" body="bad-name"/></ui>',
            '<ui><text id="PAGE" title="Page" file="page.txt" body="PAGE"/></ui>',
            '<ui><menu id="MENU" title="Menu"><string id="NAME" text="Text"/></menu></ui>',
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
            (0, 100, 1, 10, 0),
        ):
            first, last, step, scale, _ = numbers
            data = (
                f'<ui><range id="values" first="{first}" last="{last}" '
                f'step="{step}" scale="{scale}"/>'
                '<string id="NAME" text="Text"/></ui>'
            )
            with self.subTest(numbers=numbers), self.assertRaises(ValueError):
                parser.parse(data)

    def test_ranges_keep_scaled_labels_and_default_indices(self):
        document = parser.parse("""<ui>
          <range id="gain_values" first="-5" last="5" step="5"
                 scale="10" suffix=" dB" signed="true"/>
          <range id="precut" first="0" last="-10" step="-5" scale="10"/>
          <menu id="EQ" title="EQ">
            <menu id="BAND" title="Band">
              <selector id="GAIN" title="Gain" values="gain_values" slot="0" default="0"/>
            </menu>
          </menu>
        </ui>""")
        self.assertEqual(
            document.values["gain_values"].labels, ["-0.5 dB", "0.0 dB", "+0.5 dB"]
        )
        self.assertEqual(document.values["precut"].numbers, [0, -5, -10])
        self.assertEqual(document.fields[0].default_index, 1)
        self.assertIs(document.root.items[0].items[0], document.fields[0])

    def test_xml_text_and_hex_resource_ids(self):
        document = parser.parse("""<ui>
          <string id="LABEL" text="Music &amp; café &quot;mix&quot;"/>
          <screen-event screen="0x0DAD07B5" event="select" handler="Select"/>
        </ui>""")
        self.assertEqual(document.strings["LABEL"], 'Music & café "mix"')
        self.assertEqual(document.events[0].screen, 0x0DAD07B5)

    def test_native_bindings_follow_rows_but_actions_keep_storage_slots(self):
        document = parser.parse(
            menu(
                [
                    '<action id="TEST_Save" title="Save" values="choices"/>',
                    selector("SECOND", 1, 1),
                    selector("FIRST", 0),
                ]
            )
        )
        resources = Resources(
            [],
            {
                "SettingsMenu_ListItem_Legal": LEGAL_ITEM,
                "SettingsMenu_Items": 0x41,
                "SettingsMenus_Main_Screen": MAIN_SCREEN,
                "Notes_List_Screen_Alt": 0x0DAD09C3,
                "Notes_List_Screen_Alt_Default": 0x0DAD09C4,
            },
        )
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
        builder = DocumentCompiler(resources, document)
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


if __name__ == "__main__":
    unittest.main()
