# SPDX-License-Identifier: GPL-3.0-only
"""Compile menus and text pages using native FW2.0.4 resource templates."""

import struct

from . import ui
from .resource import Resource, Template
from .ui_codegen import ActionBinding, FieldBinding, emit_bindings, emit_resources

BANK_OFFSET = 0x40B930
MAIN_SCREEN = 0x0DAD0C2E
LEGAL_ITEM = 0x0DAD0C3F
LEGAL_SCREEN = 0x0DAD0C71
LEGAL_LAYOUT = 0x0DAD0C72
LEGAL_PREVIEW = 0x0DAD0C52
LEGAL_TEMPLATE = 0x0DAD0041
LEGAL_PREVIEW_LAYOUT = 0x0DAD0C05


def word(data, offset):
    return (data if isinstance(data, Resource) else Resource(data)).word(offset)


def put(data, offset, value):
    data[offset : offset + 4] = struct.pack("<I", value)


def blocks(data):
    result = []
    position = 4
    for _ in range(word(data, 0)):
        size, kind = word(data, position), word(data, position + 4)
        start = position + 8
        if start + size > len(data):
            raise ValueError("Truncated UI resource block")
        result.append((kind, Resource(data[start : start + size])))
        position = (start + size + 3) & ~3
    if position != len(data):
        raise ValueError("Unexpected UI resource block length")
    return result


def pack_blocks(items):
    result = Resource(struct.pack("<I", len(items)))
    for kind, data in items:
        result.extend(struct.pack("<II", len(data), kind))
        result.extend(data)
        result.extend(bytes(-len(result) % 4))
    return result


def serialized_string(value):
    data = value.encode("utf-8")
    return struct.pack("<I", len(data)) + data


def menu_event(item, event, handler, action, arguments):
    return (
        serialized_string(f"list.pid.{item}.{event}")
        + b"1"
        + serialized_string(handler)
        + struct.pack("<I", 1)
        + serialized_string(action)
        + struct.pack("<I", len(arguments))
        + b"".join(serialized_string(argument) for argument in arguments)
    )


class Resources:
    def __init__(self, templates):
        self.original = {
            (entry["kind"], entry["id"]): Resource.template(
                Template(
                    entry["offset"],
                    entry["bytes"],
                    {int(offset): value for offset, value in entry["words"].items()},
                )
            )
            for entry in templates
        }
        self.added = {}
        self.names = {}
        self.next_id = 0x0CF00000

    def allocate(self, name=None):
        resource_id = self.next_id
        self.next_id += 1
        if any(key[1] == resource_id for key in self.original):
            raise ValueError("CFW resource ID overlaps a stock resource")
        if name:
            if name in self.names:
                raise ValueError(f"Duplicate UI resource name: {name}")
            self.names[name] = resource_id
        return resource_id

    def add(self, kind, resource_id, data):
        key = (kind, resource_id)
        if key in self.added:
            raise ValueError(f"Duplicate CFW resource {key}")
        self.added[key] = Resource(data)

    def source(self, text):
        string_id = self.allocate()
        source_id = self.allocate()
        self.add("Str ", string_id, text.encode("utf-8") + b"\0")
        self.add(
            "SORC",
            source_id,
            pack_blocks([(0x534F5243, struct.pack("<III", 0x80, string_id, 1))]),
        )
        return source_id

    def clone_layout(
        self,
        original_id,
        new_id,
        title_source=None,
        substitutions=None,
        item_table=None,
    ):
        substitutions = substitutions or {}
        layout = blocks(self.original["SLyt", original_id])
        for _, item in layout:
            instance_list = blocks(self.original["SSin", word(item, 0)])
            list_id = self.allocate()
            put(item, 0, list_id)
            for _, instance in instance_list:
                old_instance = word(instance, 0)
                new_instance = self.allocate()
                put(instance, 0, new_instance)
                for offset in (4, 8):
                    old = word(instance, offset)
                    put(instance, offset, substitutions.get(old, old))
                for kind in ("VCrv", "VCvs"):
                    key = (kind, old_instance)
                    if key not in self.original:
                        continue
                    data = Resource(self.original[key])
                    if (
                        kind == "VCvs"
                        and title_source is not None
                        and word(instance, 4) == 0x0DAD016D
                    ):
                        bindings = blocks(data)
                        if len(bindings) != 1 or bindings[0][0] != 0x0DAD0172:
                            raise ValueError("Unexpected Legal title binding")
                        put(bindings[0][1], 12, title_source)
                        data = pack_blocks(bindings)
                    if kind == "VCvs" and item_table is not None:
                        bindings = blocks(data)
                        for binding_kind, binding in bindings:
                            if binding_kind == 0x3F1:
                                put(binding, 4, item_table)
                        data = pack_blocks(bindings)
                    self.add(kind, new_instance, data)
            self.add("SSin", list_id, pack_blocks(instance_list))
        self.add("SLyt", new_id, pack_blocks(layout))
        self.add("SEVT", new_id, self.original["SEVT", original_id])


def event(name, action):
    return (
        serialized_string(name)
        + b"1"
        + serialized_string("")
        + struct.pack("<I", 1)
        + serialized_string(action)
        + struct.pack("<I", 0)
    )


class MenuBuilder:
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
        resources.added["SEVT", layout] = (
            struct.pack("<I", 2)
            + event("button.menu.up", "navigator.PopTopScreen")
            + event("button.menu.pressandhold", "navigator.PopToMainScreen")
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
        for item in menu.items:
            if item.kind == "selector":
                row, chosen = self.selector(item)
            else:
                row_id, row = self.row(self.source(item.title))
                if item.kind == "menu":
                    chosen = self.push(row_id, item.name)
                    self.menu(item)
                else:
                    self.actions[item.name] = ActionBinding(
                        item=row_id, index=len(rows)
                    )
                    chosen = menu_event(
                        row_id,
                        "chosen",
                        item.name,
                        "navigator.SwitchLayout",
                        [menu.name + "_Layout"],
                    )
            rows.append(row)
            events.append(chosen)
        table = self.page(menu.name, menu.title, rows, events)
        for index, item in enumerate(menu.items):
            if item.kind == "selector":
                self.fields[item.slot].parent_table = table
                self.fields[item.slot].parent_index = index
            elif item.kind == "action":
                self.actions[item.name].table = table
        return table

    def insert_settings(self):
        document, resources = self.document, self.resources
        if not document.after:
            return
        title = resources.source(document.root.title)
        item_id, item = self.row(title)

        # Compose against earlier contributions to the same native menu.
        def current(kind, resource_id):
            key = kind, resource_id
            return resources.added.get(key, resources.original[key])

        items = blocks(current("ITEM", 0x41))
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
        resources.added["ITEM", 0x41] = pack_blocks(items)

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

        layouts = blocks(current("SLst", MAIN_SCREEN))
        kind, data = next(
            (kind, Resource(data))
            for kind, data in layouts
            if word(data, 0) == LEGAL_PREVIEW
        )
        put(data, 0, preview)
        layouts.append((kind, data))
        resources.added["SLst", MAIN_SCREEN] = pack_blocks(layouts)

        events = Resource(current("CEVT", MAIN_SCREEN))
        put(events, 0, word(events, 0) + 2)
        events.extend(self.push(item_id, document.root.name, document.open_action))
        events.extend(
            menu_event(
                item_id,
                "delayedselected",
                "ShowSetting_Legal",
                "navigator.SwitchLayout",
                [document.root.name + "_Preview"],
            )
        )
        resources.added["CEVT", MAIN_SCREEN] = events

    def finish_bindings(self):
        for item in self.document.actions:
            self.actions[item.name].labels = [
                self.source(label)
                for label in self.document.values[item.choices].labels
            ]


def generate(resources, sources, directory, prefix, revision, version="0.0.0-dev"):
    lines = ['#include "resources.h"', '#include "ui.h"', '#include "patch.h"', ""]
    for source in sources:
        document = ui.compile(source, directory, prefix)
        builder = MenuBuilder(resources, document)
        if document.root.kind == "menu":
            builder.menu(document.root)
        if document.root.kind == "text":
            text = (source.parent / document.root.text_file).read_text()
            display = "Development" if version.startswith("0.0.0-dev") else version
            builder.text_page(
                text.replace("{build_revision}", revision).replace(
                    "{build_version}", display
                )
            )

        string_ids = {}
        for name, text in document.strings.items():
            string_ids[name] = resource_id = resources.allocate()
            resources.add("Str ", resource_id, text.encode("utf-8") + b"\0")

        builder.insert_settings()
        builder.finish_bindings()
        lines.extend(emit_bindings(document, builder.fields, builder.actions))
        (directory / (source.stem + "_ui.h")).write_text(
            ui.header(document, string_ids)
        )

    lines.extend(emit_resources(resources))
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / "ui_resources.c"
    path.write_text("\n".join(lines))
    return path
