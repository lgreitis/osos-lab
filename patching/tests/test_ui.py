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
from patching.ui import parser
from patching.ui.codegen import emit_header, emit_resources, emit_visibility
from patching.ui.compiler import DocumentCompiler, compile_document
from patching.ui.generator import generate
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


def menu(rows):
    return (
        '<ui><values id="choices">'
        '<value number="0" label="Off"/><value number="1" label="On"/>'
        '</values><menu id="TEST" title="Test">' + "".join(rows) + "</menu></ui>"
    )


class UiTests(unittest.TestCase):
    def test_hidden_action_requires_final_menu_row(self):
        action = (
            '<action id="SAVE" title="Save" values="choices" hide-when-zero="true"/>'
        )
        info = '<info id="INFO" title="Info"/>'
        with self.assertRaisesRegex(ValueError, "final menu row"):
            parser.parse(menu([action, info]))
        document = parser.parse(menu([info, action]))
        self.assertTrue(document.root.items[-1].hide_when_zero)

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

    def test_eq_choices_use_native_models_and_shared_range_controls(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.read(root / "payload/custom_eq.ui")
        resources.allocate("CFW_EQ_Icon")
        compiled = compile_document(resources, document)
        self.assertEqual(len(compiled.fields), 21)
        self.assertEqual(
            sum(len(document.values[item.choices].labels) for item in document.fields),
            716,
        )
        rows = [
            row
            for (kind, resource_id), data in resources.added.items()
            if kind == "ITEM" and (kind, resource_id) not in resources.original
            for _, row in blocks(data)
        ]
        self.assertEqual(len(rows), 38)
        for item in document.fields:
            field = compiled.fields[item.slot]
            if document.values[item.choices].numeric:
                self.assertEqual(
                    resources.added["SCST", field.screen].data, b"CFWRange"
                )
                self.assertEqual(
                    word(event_resource(resources, "CEVT", field.screen), 0), 0
                )
                layout = resources.names[item.name + "_Layout"]
                instances = [
                    instance
                    for _, group in blocks(resources.added["SLyt", layout])
                    for _, instance in blocks(resources.current("SSin", word(group, 0)))
                ]
                title_instance = next(
                    instance
                    for instance in instances
                    if word(instance, 4) == 0x0DAD020C
                )
                title_binding = dict(
                    blocks(resources.current("VCvs", word(title_instance, 0)))
                )[0x0DAD020E]
                source = word(dict(blocks(title_binding))[0x534F5243], 0)
                title_id = word(blocks(resources.added["SORC", source])[0][1], 4)
                self.assertEqual(
                    resources.added["Str ", title_id].data,
                    document.text(item.title).default.encode() + b"\0",
                )
                control_instance = next(
                    instance
                    for instance in instances
                    if word(instance, 4) == resources.names["CFW_Range_Template"]
                )
                controls = dict(
                    blocks(resources.current("VCvs", word(control_instance, 0)))
                )
                for view in (
                    "Settings_Brightness_Bar",
                    "Settings_Brightness_Value_Text",
                ):
                    source = word(dict(blocks(controls[bindings[view]]))[0x534F5243], 0)
                    binding = blocks(resources.added["SORC", source])[0][1]
                    self.assertEqual(
                        [word(binding, offset) for offset in (0, 4, 8)],
                        [0x8900, field.property, 1],
                    )
            else:
                events = resources.added["CEVT", field.screen]
                self.assertEqual(word(events, 0), 1)
                self.assertIn(item.name.encode(), events.data)
        self.assertEqual(sum(kind == "View" for kind, _ in resources.added), 2)
        self.assertEqual(sum(kind == "SEVT" for kind, _ in resources.added), 2)
        self.assertNotIn(
            b"CFW_EQ_Set_", b"".join(data.data for data in resources.added.values())
        )
        self.assertEqual(document.root.items[1].kind, "canvas")
        self.assertEqual(document.root.items[1].title, "Graphic EQ")
        self.assertTrue(document.actions[0].hide_when_zero)
        self.assertEqual(len(resources.menu_items), 1)
        save = compiled.actions["CFW_EQ_Save"]
        save_row = next(row for row in rows if word(row, 0x30) == save.item)
        prototype = next(
            row
            for _, row in blocks(resources.stock("ITEM", "SettingsMenu_Items"))
            if word(row, 0x30) == bindings["SettingsMenu_ListItem_Legal"]
        )
        self.assertEqual(save_row[0x64:0x65].origins, prototype[0x64:0x65].origins)
        self.assertEqual(save_row.data[0x94], 1)
        source = blocks(resources.added["SORC", word(save_row, 0x68)])[0][1]
        self.assertEqual(
            [word(source, offset) for offset in (0, 4, 8)], [0x8900, save.property, 1]
        )
        self.assertIn(
            b"CFW_EQ_Save",
            resources.added["CEVT", resources.names["CFW_EQ_Screen"]].data,
        )
        self.assertEqual(len(document.values["cfw_eq_precut_values"].numbers), 101)
        self.assertEqual(
            document.values["cfw_eq_precut_values"].numbers, list(range(0, -101, -1))
        )

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

    def test_visibility_metadata_supports_multiple_entries_and_stable_keys(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        documents = [
            parser.parse(f'''<ui><music-entry id="{name}" title="{name}"
            after="MusicMenu_ListItem_Artists" screen="MediaLists_Artists_Screen"
            layout="MediaLists_Artists_Screen_Default" open="{name}" highlight="HandleMusicHilited"
            main-visible="true" music-visible="false"/></ui>''')
            for name in ("First", "Second")
        ]
        keys = []
        for order in (documents, list(reversed(documents))):
            resources = Resources(metadata["resources"], bindings)
            for document in order:
                compile_document(resources, document)
            keys.append(
                re.findall(
                    r"CFWMenu_[0-9a-f]{8}", "\n".join(emit_visibility(resources))
                )
            )
            self.assertEqual(
                [entry["defaults"] for entry in resources.visibility], [1, 1]
            )
            for menu, anchor_name in (
                ("Main", "MainMenu_List_Artists"),
                ("Music", "MusicMenu_ListItem_Artists"),
            ):
                rows = menu_rows(resources, bindings[menu + "Menu_Items"])
                anchor = next(
                    i
                    for i, (_, row) in enumerate(rows)
                    if word(row, 0x30) == bindings[anchor_name]
                )
                self.assertEqual(
                    [word(row, 0x30) for _, row in rows[anchor + 1 : anchor + 3]],
                    [entry["items"][menu] for entry in resources.visibility],
                )
        self.assertEqual(keys[0], list(reversed(keys[1])))
        for attribute in ('main-visible="yes"', 'music-visible="yes"'):
            with self.assertRaises(ValueError):
                parser.parse(f"""<ui><music-entry id="Entry" title="Entry" after="A"
                    screen="S" layout="L" open="O" highlight="H" {attribute}/></ui>""")
    def test_translation_codegen_sorts_by_resource_id_and_language(self):
        resources = Resources([])
        resources.translations.update(
            {
                (200, 4): b"last\0",
                (100, 4): b"second\0",
                (101, 0): b"third\0",
                (100, 2): b"first\0",
            }
        )
        output = "\n".join(emit_resources(resources))
        table = output.split("cfw_translations[] = {\n", 1)[1].split("};", 1)[0]
        self.assertEqual(
            table,
            "    {0x64, 2, translation_0},\n"
            "    {0x64, 4, translation_1},\n"
            "    {0x65, 0, translation_2},\n"
            "    {0xc8, 4, translation_3},\n",
        )
        for index, text in enumerate((b"first\0", b"second\0", b"third\0", b"last\0")):
            encoded = ", ".join(f"0x{byte:02x}" for byte in text)
            self.assertIn(f"translation_{index}[] = {{{encoded}}};", output)

    def test_song_info_loads_catalog_and_generates_format_signatures(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        document = parser.read(root / "payload/song_info.ui")
        self.assertEqual(
            document.text(document.root.title).translations[4], "Titelinformationen"
        )
        shared_formats = {
            "CFW_SONG_NUMBER",
            "CFW_SONG_TIME",
            "CFW_SONG_TIME_HOURS",
            "CFW_SONG_KBPS",
            "CFW_SONG_STARS",
        }
        for name in set(document.strings) - shared_formats:
            with self.subTest(string=name):
                self.assertEqual(set(document.translations[name]), set(range(1, 21)))
        resources = Resources(metadata["resources"], bindings)
        compiled = compile_document(resources, document)
        for name, signature in document.formats.items():
            self.assertEqual(resources.formats[compiled.string_ids[name]], signature)
        for item in document.root.items:
            self.assertNotIn(
                (compiled.string_ids[item.name], 4), resources.translations
            )
        self.assertIn('"uu"', "\n".join(emit_resources(resources)))
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "bad.ui"
            source.write_text("<ui/>")
            source.with_suffix(".lo").write_text(
                '<locale><format id="BAD" text="%n"/></locale>'
            )
            with self.assertRaisesRegex(ValueError, "bad.lo"):
                parser.read(source)

    def test_locale_formats_validate_types_and_allow_reordered_arguments(self):
        document = parser.parse(
            '<ui><string id="NAME" text="Name"/></ui>',
            '<locale><format id="ROW" text="%1$s: %2$u%%"/>'
            '<format id="ROW" lang="4" text="%2$u%% - %1$s %1$s"/></locale>',
        )
        self.assertEqual(document.formats, {"ROW": "su"})
        self.assertEqual(parser.format_signature("%s: %u"), "su")
        for text in ("%", "%d", "%n", "%0$s", "%9$s", "%2$s", "%1$s %1$u", "%s %2$u"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                parser.format_signature(text)
        with self.assertRaisesRegex(ValueError, "Mismatched"):
            parser.parse(
                '<ui><format id="ROW" text="%s"/>'
                '<format id="ROW" lang="4" text="%u"/></ui>'
            )
        with self.assertRaises(ValueError):
            parser.parse(
                '<ui><string id="ROW" text="Row"/></ui>',
                '<locale><string id="ROW" text="Other"/></locale>',
            )

    def test_all_payload_documents_generate_together_with_translated_examples(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        self.assert_all_payload_translations(root, resources)

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

    def assert_all_payload_translations(self, root, resources):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            result = generate(
                resources,
                sorted((root / "payload").glob("*.ui")),
                directory,
                "test-revision",
                "1.2.3",
            )
            self.assertIn("cfw_translations[]", result.read_text())
            self.assertTrue((directory / "cfw_info_ui.h").exists())
        defaults = [
            bytes(data.data)
            for (kind, _), data in resources.added.items()
            if kind == "Str "
        ]
        translations = list(resources.translations.values())
        self.assertIn(b"Dark Mode\0", defaults)
        self.assertIn(b"Dunkler Modus\0", translations)
        self.assertIn(b"RepriseOS 1.2.3\0", defaults)
        self.assertIn(b"Build: test-revision\0", defaults)
        self.assertIn(b"Custom firmware for iPod.\0", defaults)
        self.assertIn(b"Project:\nhttps://github.com/lgreitis/osos-lab\0", defaults)
        self.assertIn(b"CFW-Informationen\0", translations)
        info = parser.read(root / "payload/cfw_info.ui")
        self.assertEqual(set(info.strings), {"CFW_Info_Title"})
        self.assertFalse(any(data.startswith(b"@CFW_Info") for data in defaults))

    def test_text_page_variants_expand_build_placeholders_and_keep_literal_strings(
        self,
    ):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / "page.ui"
            source.write_text(
                '<ui><text id="PAGE" title="@TITLE" file="@body.txt"/>'
                '<string id="TITLE" text="Page"/>'
                '<string id="TITLE" lang="4" text="Seite"/>'
                '<string id="BODY" text="Version {build_version}"/>'
                '<string id="BODY" lang="4" text="Build {build_revision}&#10;@literal"/>'
                "</ui>"
            )
            (directory / "@body.txt").write_text("@BODY\n\n@@literal")
            generate(resources, [source], directory / "generated", "abc123")
        defaults = [
            bytes(data.data)
            for (kind, _), data in resources.added.items()
            if kind == "Str "
        ]
        self.assertIn(b"Version Development\0", defaults)
        self.assertIn(b"@literal\0", defaults)
        self.assertIn(b"Build abc123\n@literal\0", resources.translations.values())
        self.assertIn(b"Seite\0", resources.translations.values())

    def test_music_title_references_and_empty_translation_codegen(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.read(root / "payload/album_artists.ui")
        document.music_entry["title"] = "@TITLE"
        document.strings["TITLE"] = "Album Artists"
        document.translations["TITLE"] = {4: "Albuminterpreten"}
        compiled = compile_document(resources, document)
        self.assertEqual(
            resources.translations[compiled.string_ids["CFW_AlbumArtists_Title"], 4],
            b"Albuminterpreten\0",
        )
        output = "\n".join(emit_resources(Resources([])))
        self.assertIn("cfw_translation_count = 0;", output)
        self.assertIn("{0, 0, 0},", output)

    def test_references_compose_with_independent_language_fallbacks(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.parse(
            '<ui><values id="choices"><value number="0" label="@OFF"/></values>'
            '<range id="levels" first="0" last="1" step="1" suffix="@UNIT"/>'
            '<menu id="TEST" title="@TITLE">'
            '<selector id="STATE" title="@TITLE" values="choices" slot="0" default="0"/>'
            '<selector id="LEVEL" title="Level" values="levels" slot="1" default="0"/>'
            '<info id="INFO" title="@TITLE" unknown="@OFF"/>'
            '<action id="SAVE" title="@TITLE" values="choices"/></menu>'
            '<string id="TITLE" text="State"/><string id="TITLE" lang="4" text="Status"/>'
            '<string id="OFF" text="Off"/><string id="OFF" lang="2" text="Non"/>'
            '<string id="UNIT" text=" units"/><string id="UNIT" lang="4" text=" Einheiten"/>'
            "</ui>"
        )
        compiled = compile_document(resources, document)

        title = compiled.fields[0].title
        label = compiled.fields[0].labels[0]
        self.assertEqual(bytes(resources.added["Str ", title].data), b"State\0")
        self.assertEqual(resources.translations[title, 4], b"Status\0")
        self.assertEqual(bytes(resources.added["Str ", label].data), b"Off\0")
        self.assertEqual(resources.translations[label, 2], b"Non\0")
        self.assertNotIn((title, 2), resources.translations)
        self.assertNotIn((label, 4), resources.translations)
        self.assertEqual(compiled.actions["SAVE"].labels, [label])
        unit = compiled.fields[1].labels[0]
        self.assertEqual(resources.translations[unit, 4], b"0 Einheiten\0")
        self.assertEqual(
            resources.translations[compiled.string_ids["INFO"], 4], b"Status: Off\0"
        )
        self.assertEqual(document.text("@@literal").default, "@literal")
        self.assertEqual(document.text("literal").default, "literal")
        with self.assertRaisesRegex(ValueError, "Undefined UI string"):
            document.text("@MISSING")
        with self.assertRaisesRegex(ValueError, "Undefined UI string"):
            parser.parse('<ui><string id="OTHER" text="x"/></ui>').text("@TITLE")

    def test_string_languages_require_unique_variants_and_default(self):
        document = parser.parse(
            '<ui><string id="TITLE" lang="4" text="Deutsch"/>'
            '<string id="TITLE" text="English"/></ui>'
        )
        self.assertEqual(document.strings, {"TITLE": "English"})
        self.assertEqual(document.translations, {"TITLE": {4: "Deutsch"}})
        for declarations in (
            '<string id="TITLE" lang="4" text="Deutsch"/>',
            '<string id="TITLE" text="A"/><string id="TITLE" text="B"/>',
            '<string id="TITLE" text="A"/><string id="TITLE" lang="21" text="B"/>',
            '<string id="TITLE" text="A"/><string id="TITLE" lang="-1" text="B"/>',
            '<string id="TITLE" text="A"/><string id="title" text="B"/>',
            '<string id="TITLE" text="A"/><string id="TITLE" lang="4" text="B"/>'
            '<string id="TITLE" lang="4" text="C"/>',
        ):
            with self.subTest(declarations=declarations), self.assertRaises(ValueError):
                parser.parse("<ui>" + declarations + "</ui>")

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

    def test_album_artists_reuses_native_routes_and_adds_a_music_row(self):
        root = Path(__file__).resolve().parents[2]
        metadata = json.loads((root / "firmware/ui.json").read_text())
        shim = root / "payload/native"
        bindings = json.loads((shim / "ui.json").read_text())["bindings"]
        resources = Resources(metadata["resources"], bindings)
        document = parser.read(root / "payload/album_artists.ui")
        compiled = compile_document(resources, document)
        rows = menu_rows(resources, bindings["MusicMenu_Items"])
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
        events = b"".join(
            resources.event_extensions["CEVT", bindings["MainMenus_Music_Screen"]]
        )
        self.assertIn(f"list.pid.{item}.chosen".encode(), events)
        self.assertIn(b"CFW_AlbumArtists", events)
        self.assertIn(b"HandleMusicHilited", events)
        main_rows = menu_rows(resources, bindings["MainMenu_Items"])
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
        main_events = b"".join(
            resources.event_extensions["CEVT", bindings["MainMenus_Main_Screen"]]
        )
        self.assertIn(f"list.pid.{main_item}.chosen".encode(), main_events)
        self.assertIn(b"CFW_AlbumArtists", main_events)
        for table, inserted in (
            ("MainMenu_Items", main_anchor + 1),
            ("MusicMenu_Items", anchor + 1),
        ):
            self.assertNotIn(("ITEM", bindings[table]), resources.added)
            stock = blocks(resources.stock("ITEM", table))
            updated = menu_rows(resources, bindings[table])
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
        self.assertEqual(
            resources.added["SCST", screen].origins,
            resources.stock("SCST", "MediaLists_Artists_Screen").origins,
        )
        self.assertEqual(
            resources.event_aliases["CEVT", screen],
            bindings["MediaLists_Artists_Screen"],
        )
        self.assertEqual(
            resources.event_aliases["SEVT", layout],
            bindings["MediaLists_Artists_Screen_Default"],
        )
        self.assertNotIn(("CEVT", screen), resources.added)
        entry = resources.visibility[0]
        self.assertEqual(entry["defaults"], 2)
        self.assertEqual(entry["items"], {"Main": main_item, "Music": item})
        header = emit_header(document, compiled.string_ids)
        self.assertIn("#define CFW_AlbumArtists_Screen ", header)

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
            self.assertIn(
                b"contextualMenu.CFW_SongInfo",
                b"".join(
                    resources.event_extensions[
                        "CEVT", resources.native(item_event.screen)
                    ]
                ),
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
            ("CEVT", 10): struct.pack("<I", 0),
        }
        resources.bind_screen_event(document.events[0])
        self.assertEqual(len(resources.event_extensions["CEVT", 10]), 1)
        resources.bindings.clear()
        with self.assertRaises(KeyError):
            resources.bind_screen_event(document.events[0])

    def test_screen_events_are_one_extension_for_all_layouts(self):
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
            ("CEVT", 10): struct.pack("<I", 0),
        }
        resources.bind_screen_event(document.events[0])
        resources.bind_screen_event(ScreenEvent(10, "test.other", "OtherHandler"))
        data = b"".join(resources.event_extensions["CEVT", 10])
        self.assertIn(b"contextualMenu.CFW_PlayNext1", data)
        self.assertIn(b"CFW_PlayNext\0\0\0\0", data)
        self.assertIn(b"test.other1", data)
        self.assertFalse(resources.added)
        for layout in (20, 21):
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
                "Settings_EQ_Items": 0x47,
                "SettingsMenus_Main_Screen": MAIN_SCREEN,
                "Notes_List_Screen_Alt": 0x0DAD09C3,
                "Notes_List_Screen_Alt_Default": 0x0DAD09C4,
            },
        )
        row = bytearray(0x98)
        struct.pack_into("<I", row, 0x30, LEGAL_ITEM)
        resources.original = {
            ("ITEM", 0x41): pack_blocks([(0, row)]),
            ("ITEM", 0x47): pack_blocks([(0, row)]),
            ("SCST", MAIN_SCREEN): b"screen",
            ("SLst", 0x0DAD09C3): pack_blocks([(0, bytes(4))]),
            ("SLyt", 0x0DAD09C4): pack_blocks([(0, struct.pack("<I", 10))]),
            ("SSin", 10): pack_blocks([]),
            ("SEVT", 0x0DAD09C4): b"events",
        }
        resources.added, resources.names, resources.next_id = {}, {}, 0x0CF00000
        builder = DocumentCompiler(resources, document)
        table = builder.menu(document.root)
        parent_rows = blocks(resources.added["ITEM", table])
        self.assertEqual(word(parent_rows[2][1], 0x30), builder.fields[0].parent_item)
        self.assertEqual(word(parent_rows[1][1], 0x30), builder.fields[1].parent_item)
        self.assertEqual(
            word(parent_rows[0][1], 0x30), builder.actions["TEST_Save"].item
        )
        events = b"".join(
            bytes(data.data) if hasattr(data, "data") else data
            for (kind, _), data in resources.added.items()
            if kind == "CEVT"
        )
        self.assertIn(b"FIRST", events)
        self.assertIn(b"SECOND", events)
        self.assertNotIn(b"TEST_Set_", events)


if __name__ == "__main__":
    unittest.main()
