# SPDX-License-Identifier: GPL-3.0-only
"""Compile UI documents into native menus, text pages, and C bindings."""

import struct

from .model import CompiledDocument
from .resources import (
    Resource,
    blocks,
    event,
    menu_event,
    menu_row,
    pack_blocks,
    put,
    word,
)


class DocumentCompiler:
    def __init__(self, resources, document):
        self.resources, self.document = resources, document
        _, self.prototype = menu_row(
            blocks(resources.stock("ITEM", "SettingsMenu_Items")),
            resources.native("SettingsMenu_ListItem_Legal"),
            "Native Settings menu is missing the Legal row template",
        )
        self.string_ids = {}

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

    def row(self, title):
        data = Resource(self.prototype)
        item = self.resources.allocate()
        put(data, 0x30, item)
        put(data, 0x68, title)
        return item, (0, data)

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

    def push(self, item, name, handler=""):
        return menu_event(
            item,
            "chosen",
            handler,
            "navigator.PushScreen",
            [
                self.resources.runtime_name(name + "_Screen"),
                self.resources.runtime_name(name + "_Layout"),
                "foregroundpush",
            ],
        )

    def list_page(self, item):
        """A native scrolling list whose rows are supplied by a runtime model.

        Choosing a row runs <name>_Select; select="return" then pops the list.
        layout="inline" shows the detail beside the row text, "stacked" below it."""
        resources = self.resources
        property_id = resources.allocate(item.name + "_Rows")
        detail = resources.allocate(item.name + "_Detail")
        bindings = [(0x534F5243, struct.pack("<III", 0x8900, property_id, 1))]
        row_id, row = self.sound_check_row(bindings, detail)
        row[1][0x94:0x95] = b"\x01"
        if item.layout == "stacked":
            # Native row style 6: title with a grey, truncating subtitle line.
            put(row[1], 0x60, 6)
        self.page(
            item.name,
            item.title,
            [row],
            [
                menu_event(
                    row_id,
                    "chosen",
                    item.name + "_Select",
                    "navigator.PopTopScreen" if item.select == "return" else None,
                    [],
                ),
            ],
        )
        for suffix in ("_Rows", "_Detail", "_Screen", "_Layout"):
            name = item.name + suffix
            self.string_ids[name] = resources.names[name]

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

    def sound_check_row(self, bindings, value):
        resources = self.resources
        kind, row = menu_row(
            blocks(resources.stock("ITEM", "SettingsMenu_Items")),
            resources.native("SettingsMenu_ListItem_SoundCheck"),
            "Native Settings menu is missing the Sound Check row template",
        )
        item_id = resources.allocate()
        source = resources.allocate()
        # Sound Check's second source binds the blue inline text (destination 10).
        bindings.append((0x534F5243, struct.pack("<III", 0x8900, value, 10)))
        resources.add("SORC", source, pack_blocks(bindings))
        put(row, 0x30, item_id)
        put(row, 0x68, source)
        return item_id, (kind, row)

    def toggle_row(self, title):
        name = self.document.root.name
        value = self.resources.allocate(name + "_Value")
        bindings = blocks(self.resources.current("SORC", title))
        item_id, row = self.sound_check_row(bindings, value)
        self.string_ids.update({name + "_Item": item_id, name + "_Value": value})
        return item_id, row

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
                [self.resources.runtime_name(document.root.name + "_Preview")],
            ),
        ]
        self.resources.extend_events(
            "CEVT", self.resources.native("SettingsMenus_Main_Screen"), events
        )


def compile_document(resources, document, text=""):
    compiled = CompiledDocument()
    if document.music_entry:
        compile_music_entry(resources, document.music_entry, compiled)
    if document.main_entry:
        compile_main_entry(resources, document.main_entry, compiled)
    for binding in document.events:
        resources.bind_screen_event(binding)
    builder = None
    if document.root:
        builder = DocumentCompiler(resources, document)
        if document.root.kind == "text":
            builder.text_page(text)
        if document.root.kind == "list":
            builder.list_page(document.root)
        if document.root.kind == "canvas":
            builder.canvas_page(document.root)

    for name, text in document.strings.items():
        compiled.string_ids[name] = resource_id = resources.allocate()
        resources.add("Str ", resource_id, text.encode("utf-8") + b"\0")

    if builder is not None:
        builder.insert_settings()
        compiled.string_ids.update(builder.string_ids)
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
        anchor = resources.native(anchor_name)
        kind, row = menu_row(
            rows, anchor, f"{menu} anchor {anchor_name} missing for {name}"
        )
        item = resources.allocate(name + "_" + menu + "Item")
        visibility["items"][menu] = item
        visibility["anchors"][menu] = anchor
        visibility["defaults"] |= int(visible) << (menu == "Music")
        compiled.string_ids[name + "_" + menu + "Item"] = item
        put(row, 0x30, item)
        put(row, 0x60, 5)
        put(row, 0x64, visible)
        put(row, 0x68, title)
        resources.insert_menu_item(table, anchor, (kind, row))
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


def native_id(resources, value):
    """A native resource by binding name, or by number (decimal or 0x hexadecimal)."""
    return resources.bindings[value] if value in resources.bindings else int(value, 0)


def compile_main_entry(resources, entry, compiled):
    """Add a main menu row after the native row `after`, with a live preview.

    Runtime availability and the saved Main Menu checkbox control visibility.
    The "chosen" event runs `open`; "delayedselected" runs `hilite` and switches to a clone of the
    native watermark layout `preview` (picture plus up to three text lines). The first line is
    bound to the Settings-model string property <id>_Count, so C code can change it while the
    layout is cached: it answers the property with the Str <id>_Message and notifies the model.
    The other lines are left empty.

    Generated names: <id>_MainItem, <id>_Preview (layout), <id>_Title and <id>_Message (Str),
    <id>_Count (property)."""
    name = entry["id"]
    item = resources.allocate(name + "_MainItem")
    title_id = resources.allocate(name + "_Title")
    title = resources.source(entry["title"], title_id)
    message_id = resources.allocate(name + "_Message")
    resources.add("Str ", message_id, entry.get("message", "").encode("utf-8") + b"\0")
    count = resources.allocate(name + "_Count")
    message = resources.property_source(0x8900, count)
    empty = resources.source("")
    preview = resources.allocate(name + "_Preview")
    compiled.string_ids.update(
        {
            name + "_MainItem": item,
            name + "_Title": title_id,
            name + "_Message": message_id,
            name + "_Count": count,
            name + "_Preview": preview,
        }
    )

    # The row is cloned from its anchor: same template, hidden by default.
    table = resources.native("MainMenu_Items")
    rows = blocks(resources.current("ITEM", table))
    after = native_id(resources, entry["after"])
    kind, row = menu_row(
        rows, after, f"Main menu anchor {entry['after']} missing for {name}"
    )
    put(row, 0x30, item)
    put(row, 0x64, 0)
    # The low half of +0x5c is the row's native index. Now Playing's is special-cased by Apple
    # and would open Now Playing, so use Artists' (4), as the Album Artists main row does.
    put(row, 0x5C, (word(row, 0x5C) & 0xFFFF0000) | 4)
    put(row, 0x68, title)
    resources.insert_menu_item(table, after, (kind, row))
    resources.visibility.append(
        {
            "id": name,
            "title": title_id,
            "items": {"Main": item, "Music": 0},
            # Now Playing has no checkbox; Sleep is the last native visibility choice.
            "anchors": {"Main": resources.native("MainMenu_List_Sleep"), "Music": 0},
            "defaults": 1,
            "available": 0,
        }
    )

    # The preview layout: its text views read our sources instead of the native message.
    views = {
        resources.native("PreviewArea_Watermark_Message"): message,
        resources.native("PreviewArea_Watermark_Message2"): empty,
        resources.native("PreviewArea_Watermark_Message3"): empty,
    }
    original = native_id(resources, entry["preview"])
    view_bindings = {}
    rebound = 0
    for _, layout in blocks(resources.stock("SLyt", original)):
        for _, instance in blocks(resources.stock("SSin", word(layout, 0))):
            key = ("VCvs", word(instance, 0))
            if key not in resources.original:
                continue
            bindings = blocks(resources.original[key])
            changed = False
            for view, binding in bindings:
                if view in views:
                    put(binding, len(binding) - 4, views[view])
                    changed = True
                    rebound += views[view] == message
            if changed:
                view_bindings[word(instance, 4)] = pack_blocks(bindings)
    resources.clone_layout(original, preview, view_bindings=view_bindings)
    if rebound != 1:
        raise ValueError(
            f"{name}: preview layout {entry['preview']} has {rebound} message views (want 1)"
        )

    screen = resources.native("MainMenus_Main_Screen")
    layouts = blocks(resources.current("SLst", screen))
    kind, data = layouts[0]
    data = Resource(data)
    put(data, 0, preview)
    layouts.append((kind, data))
    resources.replace("SLst", screen, pack_blocks(layouts))

    resources.extend_events(
        "CEVT",
        screen,
        [
            menu_event(item, "chosen", entry["open"]),
            menu_event(
                item,
                "delayedselected",
                entry["hilite"],
                "navigator.SwitchLayout",
                [resources.runtime_name(name + "_Preview")],
            ),
        ],
    )
