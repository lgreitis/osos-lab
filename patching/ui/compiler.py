# SPDX-License-Identifier: GPL-3.0-only
"""Compile UI documents into native menus, text pages, and C bindings."""

import struct

from .model import ActionBinding, CompiledDocument, FieldBinding
from .resources import (
    LEGAL_ITEM,
    LEGAL_LAYOUT,
    LEGAL_PREVIEW,
    LEGAL_PREVIEW_LAYOUT,
    LEGAL_SCREEN,
    LEGAL_TEMPLATE,
    MAIN_SCREEN,
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
                for _, data in blocks(resources.original["ITEM", 0x41])
                if word(data, 0x30) == LEGAL_ITEM
            ),
            None,
        )
        if self.prototype is None:
            raise ValueError("Native Settings menu is missing the Legal row template")
        self.sources = {}
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
        resources.add("TMLT", template_id, resources.original["TMLT", LEGAL_TEMPLATE])

        resources.clone_layout(
            LEGAL_LAYOUT, layout_id, title_source, {LEGAL_TEMPLATE: template_id}
        )
        resources.add("SCST", screen_id, resources.original["SCST", LEGAL_SCREEN])
        resources.add("CEVT", screen_id, resources.original["CEVT", LEGAL_SCREEN])
        layouts = blocks(resources.original["SLst", LEGAL_SCREEN])
        put(layouts[0][1], 0, layout_id)
        resources.add("SLst", screen_id, pack_blocks(layouts))

    def paragraph_views(self, text):
        resources = self.resources
        # Each Legal paragraph sizes itself to its text and anchors below its predecessor.
        legal_views = blocks(resources.original["View", LEGAL_TEMPLATE])
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

    def page(self, name, title, rows, events):
        resources = self.resources
        screen = resources.allocate(name + "_Screen")
        layout = resources.allocate(name + "_Layout")
        table = resources.allocate()
        resources.add("ITEM", table, pack_blocks(rows))
        resources.clone_layout(
            0x0DAD09C4, layout, resources.source(title), item_table=table
        )
        resources.add("SCST", screen, resources.original["SCST", MAIN_SCREEN])
        resources.add("CEVT", screen, struct.pack("<I", len(events)) + b"".join(events))
        layouts = blocks(resources.original["SLst", 0x0DAD09C3])
        put(layouts[0][1], 0, layout)
        resources.add("SLst", screen, pack_blocks(layouts))
        resources.replace(
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
        values = self.document.values[item.choices].labels
        labels = [self.source(value) for value in values]
        marked = [self.source(value + " *") for value in values]
        summaries = [self.source(item.title + ": " + value) for value in values]
        rows, events, items = [], [], []
        for index, label in enumerate(labels):
            row_id, row = self.row(
                marked[index] if index == item.default_index else label
            )
            items.append(row_id)
            rows.append(row)
            events.append(
                menu_event(
                    row_id,
                    "chosen",
                    f"{self.document.root.name}_Set_{item.slot:02d}_{index:02d}",
                    "navigator.PopTopScreen",
                    [],
                )
            )
        table = self.page(item.name, item.title, rows, events)
        parent_item, parent_row = self.row(summaries[item.default_index])
        self.fields[item.slot] = FieldBinding(
            parent_item=parent_item,
            selector_table=table,
            default=item.default_index,
            labels=labels,
            marked=marked,
            summaries=summaries,
            items=items,
        )
        return parent_row, self.push(parent_item, item.name)

    def menu(self, menu):
        rows, events = [], []
        for index, item in enumerate(menu.items):
            row, chosen = self.menu_item(menu, item, index)
            rows.append(row)
            events.append(chosen)
        table = self.page(menu.name, menu.title, rows, events)
        self.bind_menu_table(menu, table)
        return table

    def menu_item(self, menu, item, index):
        if item.kind == "selector":
            return self.selector(item)
        row_id, row = self.row(self.source(item.title))
        if item.kind == "menu":
            chosen = self.push(row_id, item.name)
            self.menu(item)
        else:
            self.actions[item.name] = ActionBinding(item=row_id, index=index)
            chosen = menu_event(
                row_id,
                "chosen",
                item.name,
                "navigator.SwitchLayout",
                [menu.name + "_Layout"],
            )
        return row, chosen

    def bind_menu_table(self, menu, table):
        for index, item in enumerate(menu.items):
            if item.kind == "selector":
                self.fields[item.slot].parent_table = table
                self.fields[item.slot].parent_index = index
            elif item.kind == "action":
                self.actions[item.name].table = table

    def insert_settings(self):
        if not self.document.after:
            return
        title = self.resources.source(self.document.root.title)
        item_id, item = self.row(title)
        self.insert_settings_row(item)
        self.add_settings_preview(title)
        self.bind_settings_events(item_id)

    def insert_settings_row(self, item):
        document, resources = self.document, self.resources
        items = blocks(resources.current("ITEM", 0x41))
        anchor = next(
            (
                i
                for i, (_, data) in enumerate(items)
                if word(data, 0x30) == document.after
            ),
            None,
        )
        if anchor is None:
            raise ValueError(
                f"Settings anchor {document.after:#x} missing for {document.root.name}"
            )
        items.insert(anchor + 1, item)
        resources.replace("ITEM", 0x41, pack_blocks(items))

    def add_settings_preview(self, title):
        document, resources = self.document, self.resources
        preview = resources.allocate(document.root.name + "_Preview")
        preview_view = resources.allocate()
        data = Resource(resources.original["VLyt", LEGAL_PREVIEW_LAYOUT])
        put(data, 0x24, title)
        resources.add("VLyt", preview_view, data)
        resources.add(
            "TEVT", preview_view, resources.original["TEVT", LEGAL_PREVIEW_LAYOUT]
        )
        resources.clone_layout(
            LEGAL_PREVIEW, preview, substitutions={LEGAL_PREVIEW_LAYOUT: preview_view}
        )

        layouts = blocks(resources.current("SLst", MAIN_SCREEN))
        kind, data = next(
            (kind, Resource(data))
            for kind, data in layouts
            if word(data, 0) == LEGAL_PREVIEW
        )
        put(data, 0, preview)
        layouts.append((kind, data))
        resources.replace("SLst", MAIN_SCREEN, pack_blocks(layouts))

    def bind_settings_events(self, item_id):
        document = self.document
        events = [
            self.push(item_id, document.root.name, document.open_action),
            menu_event(
                item_id,
                "delayedselected",
                "ShowSetting_Legal",
                "navigator.SwitchLayout",
                [document.root.name + "_Preview"],
            ),
        ]
        self.resources.extend_events("CEVT", MAIN_SCREEN, events)

    def finish_bindings(self):
        for item in self.document.actions:
            self.actions[item.name].labels = [
                self.source(label)
                for label in self.document.values[item.choices].labels
            ]


def compile_document(resources, document, text=""):
    compiled = CompiledDocument()
    for binding in document.events:
        resources.bind_screen_event(binding)
    builder = None
    if document.root:
        builder = DocumentCompiler(resources, document)
        if document.root.kind == "menu":
            builder.menu(document.root)
        if document.root.kind == "text":
            builder.text_page(text)

    for name, text in document.strings.items():
        compiled.string_ids[name] = resource_id = resources.allocate()
        resources.add("Str ", resource_id, text.encode("utf-8") + b"\0")

    if builder is not None:
        builder.insert_settings()
        builder.finish_bindings()
        compiled.fields = builder.fields
        compiled.actions = builder.actions
    return compiled
