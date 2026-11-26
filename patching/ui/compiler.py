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
        self.string_ids[root.name + "_CanvasTemplate"] = template
        prototype = blocks(resources.stock("View", "Settings_Legal_Template"))[0][1]
        view = Resource(prototype[:0x5C])
        # Cover the status bar's final divider row. Bottom/right are independent extents.
        view[:0x30] = struct.pack("<12i", 0, 0, 0, 23, 0, 0, 217, 0, 6, 320, 0, 5)
        put(view, 0x30, resources.allocate())
        view_type = int.from_bytes(root.view_type.encode("ascii"), "big")
        put(view, 0x4C, view_type)
        resources.add("View", template, pack_blocks([(view_type, view)]))
        resources.add(
            "TMLT", template, resources.stock("TMLT", "Settings_Brightness_Template")
        )
        title = resources.allocate()
        self.string_ids.update(
            {
                root.name + "_Screen": screen,
                root.name + "_Layout": layout,
                root.name + "_Title": title,
            }
        )
        resources.clone_layout(
            resources.native("Settings_Brightness_Screen_Default"),
            layout,
            resources.source(root.title, title)
            if root.title
            else resources.property_source(0x8900, title),
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
        resources.add("SCST", screen, root.factory.encode("ascii"))
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

    def image_preview(self, data, name=None):
        document, resources = self.document, self.resources
        sources = {}
        for view, suffix in (
            ("Settings_Icon_View", "Image"),
            ("Settings_InfoDetail_View", "Text"),
        ):
            if name is None and suffix == "Text" and document.preview_caption:
                sources[resources.native(view)] = resources.source(
                    document.preview_caption
                )
                continue
            if name is None and suffix == "Image" and document.preview_image:
                property_id = resources.names[document.preview_image]
            else:
                key = (name or document.root.name) + "_Preview" + suffix
                self.string_ids[key] = property_id = resources.allocate(key)
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
