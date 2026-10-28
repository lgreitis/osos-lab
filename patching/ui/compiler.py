# SPDX-License-Identifier: GPL-3.0-only
"""Compile UI documents into native menus, text pages, and C bindings."""

import struct

from .model import ActionBinding, CompiledDocument, FieldBinding
from .resources import (
    Resource,
    blocks,
    event,
    menu_event,
    pack_blocks,
    put,
    word,
)


class DocumentCompiler:
    def __init__(self, resources, document):
        self.resources, self.document = resources, document
        self.prototype = next(
            (
                data
                for _, data in blocks(resources.stock("ITEM", "SettingsMenu_Items"))
                if word(data, 0x30) == resources.native("SettingsMenu_ListItem_Legal")
            ),
            None,
        )
        if self.prototype is None:
            raise ValueError("Native Settings menu is missing the Legal row template")
        self.sources = {}
        self.string_ids = {}
        self.fields: dict[int, FieldBinding] = {}
        self.actions: dict[str, ActionBinding] = {}

    def text_page(self, text):
        resources, root = self.resources, self.document.root
        screen_id = resources.allocate(root.name + "_Screen")
        layout_id = resources.allocate(root.name + "_Layout")
        template_id = resources.allocate()
        title_source = resources.source(root.title)
        views = self.paragraph_views(text)
        resources.add("View", template_id, pack_blocks(views))
        resources.add(
            "TMLT",
            template_id,
            resources.stock("TMLT", "Settings_Legal_Template"),
        )

        resources.clone_layout(
            resources.native("Settings_Legal_Screen_Default"),
            layout_id,
            title_source,
            {resources.native("Settings_Legal_Template"): template_id},
        )
        resources.add(
            "SCST",
            screen_id,
            resources.stock("SCST", "Settings_Legal_Screen"),
        )
        resources.alias_events(
            "CEVT", screen_id, resources.native("Settings_Legal_Screen")
        )
        layouts = blocks(resources.stock("SLst", "Settings_Legal_Screen"))
        put(layouts[0][1], 0, layout_id)
        resources.add("SLst", screen_id, pack_blocks(layouts))

    def canvas_page(self, root):
        resources = self.resources
        screen = resources.allocate(root.name + "_Screen")
        layout = resources.allocate(root.name + "_Layout")
        template = resources.allocate()
        prototype = blocks(resources.stock("View", "Settings_Legal_Template"))[0][1]
        view = Resource(prototype[:0x5C])
        # Cover the status bar's final divider row. Bottom/right are independent extents.
        view[:0x30] = struct.pack("<12i", 0, 0, 0, 23, 0, 0, 217, 0, 6, 320, 0, 5)
        put(view, 0x30, resources.allocate())
        put(view, 0x4C, 0x63667763)  # cfwc
        resources.add("View", template, pack_blocks([(0x63667763, view)]))
        resources.add(
            "TMLT", template, resources.stock("TMLT", "Settings_Brightness_Template")
        )
        resources.clone_layout(
            resources.native("Settings_Brightness_Screen_Default"),
            layout,
            resources.source(root.title),
            {resources.native("Settings_Brightness_Template"): template},
            view_bindings={
                resources.native("Settings_Brightness_Template"): pack_blocks([])
            },
        )
        for _, group in blocks(resources.added["SLyt", layout]):
            instances_id = word(group, 0)
            instances = blocks(resources.added["SSin", instances_id])
            for _, instance in instances:
                if word(instance, 4) == template:
                    put(instance, 0x10, word(instance, 0x10) & ~0x00FF0000)
            resources.replace("SSin", instances_id, pack_blocks(instances))
        resources.add("SCST", screen, b"CFWCanvas")
        resources.set_events("CEVT", screen, struct.pack("<I", 0))
        layouts = blocks(resources.stock("SLst", "Settings_Brightness_Screen"))
        put(layouts[0][1], 0, layout)
        resources.add("SLst", screen, pack_blocks(layouts))
        resources.set_events(
            "SEVT",
            layout,
            struct.pack("<I", 2)
            + event("button.menu.up", "navigator.PopTopScreen")
            + event("button.menu.pressandhold", "navigator.PopToMainScreen"),
        )

    def paragraph_views(self, text):
        resources = self.resources
        # Each Legal paragraph sizes itself to its text and anchors below its predecessor.
        legal_views = blocks(resources.stock("View", "Settings_Legal_Template"))
        views = []
        previous = 0
        paragraphs = [
            paragraph.strip()
            for paragraph in text.strip().split("\n\n")
            if paragraph.strip()
        ]
        for paragraph in paragraphs:
            kind, prototype = legal_views[1]
            view = Resource(prototype)
            view_id = resources.allocate()
            put(view, 0x0C, 15 if previous == 0 else 12)
            put(view, 0x10, previous)
            put(view, 0x14, 0 if previous == 0 else 3)
            put(view, 0x30, view_id)
            put(view, 0x64, resources.source(paragraph))
            views.append((kind, view))
            previous = view_id
        kind, prototype = legal_views[-1]
        margin = Resource(prototype)
        put(margin, 0x10, previous)
        put(margin, 0x30, resources.allocate())
        views.append((kind, margin))
        return views

    def source(self, text):
        if text not in self.sources:
            self.sources[text] = self.resources.source(text)
        return self.sources[text]

    def row(self, title):
        data = Resource(self.prototype)
        item = self.resources.allocate()
        put(data, 0x30, item)
        put(data, 0x68, title)
        return item, (0, data)

    def model_row(self, property_id):
        item, row = self.row(self.resources.property_source(0x8900, property_id))
        row[1][0x94:0x95] = b"\x01"
        return item, row

    def page(self, name, title, rows, events):
        resources = self.resources
        screen = resources.allocate(name + "_Screen")
        layout = resources.allocate(name + "_Layout")
        table = resources.allocate()
        resources.add("ITEM", table, pack_blocks(rows))
        resources.clone_layout(
            resources.native("Notes_List_Screen_Alt_Default"),
            layout,
            resources.source(title),
            item_table=table,
        )
        resources.add(
            "SCST",
            screen,
            resources.stock("SCST", "SettingsMenus_Main_Screen"),
        )
        resources.set_events(
            "CEVT", screen, struct.pack("<I", len(events)) + b"".join(events)
        )
        layouts = blocks(resources.stock("SLst", "Notes_List_Screen_Alt"))
        put(layouts[0][1], 0, layout)
        resources.add("SLst", screen, pack_blocks(layouts))
        resources.set_events(
            "SEVT",
            layout,
            struct.pack("<I", 2)
            + event("button.menu.up", "navigator.PopTopScreen")
            + event("button.menu.pressandhold", "navigator.PopToMainScreen"),
        )
        return table

    def push(self, item, name, handler=""):
        return menu_event(
            item,
            "chosen",
            handler,
            "navigator.PushScreen",
            [name + "_Screen", name + "_Layout", "foregroundpush"],
        )

    def selector(self, item):
        resources = self.resources
        property_id, summary = resources.allocate(), resources.allocate()
        if self.document.values[item.choices].numeric:
            self.range_page(item, property_id)
        else:
            kind, prototype = blocks(resources.stock("ITEM", "Settings_EQ_Items"))[0]
            row = Resource(prototype)
            row_id = resources.allocate()
            put(row, 0x30, row_id)
            put(row, 0x68, resources.property_source(0x8900, property_id))
            row[0x94:0x95] = b"\x01"
            self.page(
                item.name,
                item.title,
                [(kind, row)],
                [menu_event(row_id, "chosen", item.name, "navigator.PopTopScreen", [])],
            )
        parent_item, parent_row = self.model_row(summary)
        self.fields[item.slot] = FieldBinding(
            parent_item=parent_item,
            property=property_id,
            summary_property=summary,
            screen=resources.names[item.name + "_Screen"],
        )
        return parent_row, self.push(parent_item, item.name)

    def range_page(self, item, property_id):
        resources = self.resources
        template_name = "CFW_Range_Template"
        if template_name not in resources.names:
            template = resources.allocate(template_name)
            views = [
                (kind, data)
                for kind, data in blocks(
                    resources.stock("View", "Settings_Brightness_Template")
                )
                if kind in (0x70676F67, 0x74657874)
            ]
            resources.add("View", template, pack_blocks(views))
            resources.add(
                "TMLT",
                template,
                resources.stock("TMLT", "Settings_Brightness_Template"),
            )
        template = resources.names[template_name]
        screen = resources.allocate(item.name + "_Screen")
        layout = resources.allocate(item.name + "_Layout")
        resources.add("SCST", screen, b"CFWRange")
        resources.set_events("CEVT", screen, struct.pack("<I", 0))
        source = resources.property_source(0x8900, property_id)
        binding = pack_blocks(
            [
                (
                    resources.native(view),
                    pack_blocks([(0x534F5243, struct.pack("<I", source))]),
                )
                for view in (
                    "Settings_Brightness_Bar",
                    "Settings_Brightness_Value_Text",
                )
            ]
        )
        resources.clone_layout(
            resources.native("Settings_Brightness_Screen_Default"),
            layout,
            resources.source(item.title),
            {resources.native("Settings_Brightness_Template"): template},
            view_bindings={resources.native("Settings_Brightness_Template"): binding},
        )
        layouts = blocks(resources.stock("SLst", "Settings_Brightness_Screen"))
        put(layouts[0][1], 0, layout)
        resources.add("SLst", screen, pack_blocks(layouts))
        resources.set_events(
            "SEVT",
            layout,
            struct.pack("<I", 3)
            + event("button.select.up", "navigator.PopTopScreen")
            + event("button.menu.up", "navigator.PopTopScreen")
            + event("button.menu.pressandhold", "navigator.PopToMainScreen"),
        )

    def menu(self, menu):
        rows, events = [], []
        for item in menu.items:
            row, chosen = self.menu_item(menu, item)
            rows.append(row)
            if chosen is not None:
                events.append(chosen)
        return self.page(menu.name, menu.title, rows, events)

    def menu_item(self, menu, item):
        if item.kind == "selector":
            return self.selector(item)
        if item.kind == "info":
            string_id = self.resources.allocate()
            self.string_ids[item.name] = string_id
            _, row = self.row(
                self.resources.source(item.title + ": Unknown", string_id)
            )
            # Native text rows omit the submenu arrow; no chosen event is bound.
            put(row[1], 0x60, 2)
            return row, None
        if item.kind in ("menu", "canvas"):
            row_id, row = self.row(self.source(item.title))
            chosen = self.push(row_id, item.name)
            if item.kind == "canvas":
                self.canvas_page(item)
            else:
                self.menu(item)
        else:
            property_id = self.resources.allocate()
            row_id, row = self.model_row(property_id)
            put(row[1], 0x60, 2)
            self.actions[item.name] = ActionBinding(item=row_id, property=property_id)
            chosen = menu_event(
                row_id,
                "chosen",
                item.name,
                "navigator.SwitchLayout",
                [menu.name + "_Layout"],
            )
        return row, chosen

    def insert_settings(self):
        if not self.document.after:
            return
        title = self.resources.source(self.document.root.title)
        if self.document.root.kind == "toggle":
            item_id, item = self.toggle_row(title)
        else:
            item_id, item = self.row(title)
        self.insert_settings_row(item)
        self.add_settings_preview(title)
        self.bind_settings_events(item_id)

    def toggle_row(self, title):
        resources = self.resources
        item_id = resources.allocate()
        kind, prototype = next(
            (kind, data)
            for kind, data in blocks(resources.stock("ITEM", "SettingsMenu_Items"))
            if word(data, 0x30) == resources.native("SettingsMenu_ListItem_SoundCheck")
        )
        row = Resource(prototype)
        name = self.document.root.name
        value = resources.allocate(name + "_Value")
        source = resources.allocate()
        bindings = blocks(resources.current("SORC", title))
        # Sound Check's second source binds the blue inline text (destination 10).
        bindings.append((0x534F5243, struct.pack("<III", 0x8900, value, 10)))
        resources.add("SORC", source, pack_blocks(bindings))
        put(row, 0x30, item_id)
        put(row, 0x68, source)
        self.string_ids.update({name + "_Item": item_id, name + "_Value": value})
        return item_id, (kind, row)

    def insert_settings_row(self, item):
        document, resources = self.document, self.resources
        resources.insert_menu_item(
            resources.native("SettingsMenu_Items"),
            resources.native(document.after),
            item,
        )

    def add_settings_preview(self, title):
        document, resources = self.document, self.resources
        style = "Brightness" if document.preview == "image" else "Legal"
        template = resources.native(f"SettingsInfo_Template_{style}_Layout")
        layout = resources.native(f"SettingsMenus_{style}_Layout")
        preview = resources.allocate(document.root.name + "_Preview")
        preview_view = resources.allocate()
        data = Resource(resources.original["VLyt", template])
        put(data, 0x24, title)
        if document.preview == "image":
            data = self.image_preview(data)
        resources.add("VLyt", preview_view, data)
        resources.alias_events("TEVT", preview_view, template)
        resources.clone_layout(
            layout,
            preview,
            substitutions={template: preview_view},
        )

        layouts = blocks(
            resources.current("SLst", resources.native("SettingsMenus_Main_Screen"))
        )
        kind, data = next(
            (kind, Resource(data)) for kind, data in layouts if word(data, 0) == layout
        )
        put(data, 0, preview)
        layouts.append((kind, data))
        resources.replace(
            "SLst", resources.native("SettingsMenus_Main_Screen"), pack_blocks(layouts)
        )

    def image_preview(self, data):
        document, resources = self.document, self.resources
        sources = {}
        for view, suffix in (
            ("Settings_Icon_View", "Image"),
            ("Settings_InfoDetail_View", "Text"),
        ):
            if suffix == "Text" and document.preview_caption:
                sources[resources.native(view)] = resources.source(
                    document.preview_caption
                )
                continue
            if suffix == "Image" and document.preview_image:
                property_id = resources.names[document.preview_image]
            else:
                name = document.root.name + "_Preview" + suffix
                self.string_ids[name] = property_id = resources.allocate(name)
            sources[resources.native(view)] = resources.property_source(
                0x8900, property_id
            )
        views = []
        for view, options in blocks(data):
            if view in sources:
                options = pack_blocks(
                    [
                        (
                            kind,
                            struct.pack("<I", sources[view])
                            if kind == 0x534F5243
                            else value,
                        )
                        for kind, value in blocks(options)
                    ]
                )
            views.append((view, options))
        return pack_blocks(views)

    def bind_settings_events(self, item_id):
        document = self.document
        if document.root.kind == "toggle":
            chosen = menu_event(item_id, "chosen", document.root.name + "_Toggle")
        else:
            chosen = self.push(item_id, document.root.name, document.open_action)
        events = [
            chosen,
            menu_event(
                item_id,
                "delayedselected",
                "" if document.preview == "image" else "ShowSetting_Legal",
                "navigator.SwitchLayout",
                [document.root.name + "_Preview"],
            ),
        ]
        self.resources.extend_events(
            "CEVT", self.resources.native("SettingsMenus_Main_Screen"), events
        )


def compile_document(resources, document, text=""):
    compiled = CompiledDocument()
    if document.music_entry:
        compile_music_entry(resources, document.music_entry, compiled)
    for binding in document.events:
        resources.bind_screen_event(binding)
    builder = None
    if document.root:
        builder = DocumentCompiler(resources, document)
        if document.root.kind == "menu":
            builder.menu(document.root)
            for suffix in ("_Screen", "_Layout"):
                name = document.root.name + suffix
                compiled.string_ids[name] = resources.names[name]
        if document.root.kind == "text":
            builder.text_page(text)
        if document.root.kind == "canvas":
            builder.canvas_page(document.root)

    for name, text in document.strings.items():
        compiled.string_ids[name] = resource_id = resources.allocate()
        resources.add("Str ", resource_id, text.encode("utf-8") + b"\0")

    if builder is not None:
        builder.insert_settings()
        compiled.string_ids.update(builder.string_ids)
        compiled.fields = builder.fields
        compiled.actions = builder.actions
    return compiled


def compile_music_entry(resources, entry, compiled):
    name = entry["id"]
    screen = resources.allocate(name + "_Screen")
    layout = resources.allocate(name + "_Layout")
    title_id = resources.allocate(name + "_Title")
    title = resources.source(entry["title"], title_id)
    resources.clone_layout(resources.native(entry["layout"]), layout, title)
    resources.add("SCST", screen, resources.stock("SCST", entry["screen"]))
    resources.alias_events("CEVT", screen, resources.native(entry["screen"]))
    layouts = blocks(resources.stock("SLst", entry["screen"]))
    if len(layouts) != 1:
        raise ValueError("Music entry expects a single-layout native screen")
    put(layouts[0][1], 0, layout)
    resources.add("SLst", screen, pack_blocks(layouts))
    compiled.string_ids.update(
        {name + "_Screen": screen, name + "_Layout": layout, name + "_Title": title_id}
    )

    visibility = {
        "id": name,
        "title": title_id,
        "items": {},
        "anchors": {},
        "defaults": 0,
    }
    for menu, anchor_name, visible in (
        ("Music", entry["after"], entry["music-visible"] == "true"),
        (
            "Main",
            entry.get(
                "main-after",
                entry["after"].replace("MusicMenu_ListItem_", "MainMenu_List_"),
            ),
            entry["main-visible"] == "true",
        ),
    ):
        table = resources.native(menu + "Menu_Items")
        rows = blocks(resources.stock("ITEM", table))
        anchor = next(
            (
                i
                for i, (_, row) in enumerate(rows)
                if word(row, 0x30) == resources.native(anchor_name)
            ),
            None,
        )
        if anchor is None:
            raise ValueError(f"{menu} anchor {anchor_name} missing for {name}")
        kind, prototype = rows[anchor]
        row = Resource(prototype)
        item = resources.allocate(name + "_" + menu + "Item")
        visibility["items"][menu] = item
        visibility["anchors"][menu] = resources.native(anchor_name)
        visibility["defaults"] |= int(visible) << (menu == "Music")
        compiled.string_ids[name + "_" + menu + "Item"] = item
        put(row, 0x30, item)
        put(row, 0x60, 5)
        put(row, 0x64, visible)
        put(row, 0x68, title)
        resources.insert_menu_item(table, resources.native(anchor_name), (kind, row))
        events = []
        for event_name, handler in (
            ("chosen", entry["open"]),
            ("delayedselected", entry["highlight"]),
        ):
            events.append(menu_event(item, event_name, handler))
        resources.extend_events(
            "CEVT", resources.native(f"MainMenus_{menu}_Screen"), events
        )
    resources.visibility.append(visibility)
