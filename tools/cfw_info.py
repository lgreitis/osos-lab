"""Build CFW info resources from the user's FW2.0.4 UI templates."""

import struct
from pathlib import Path

BANK_OFFSET = 0x40B930
MAIN_SCREEN = 0x0DAD0C2E
LEGAL_ITEM = 0x0DAD0C3F
LEGAL_SCREEN = 0x0DAD0C71
LEGAL_LAYOUT = 0x0DAD0C72
LEGAL_PREVIEW = 0x0DAD0C52
LEGAL_TEMPLATE = 0x0DAD0041
LEGAL_PREVIEW_LAYOUT = 0x0DAD0C05


def word(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def put(data, offset, value):
    struct.pack_into("<I", data, offset, value)


def blocks(data):
    result = []
    position = 4
    for _ in range(word(data, 0)):
        size, kind = struct.unpack_from("<II", data, position)
        start = position + 8
        if start + size > len(data):
            raise ValueError("Truncated UI resource block")
        result.append((kind, bytearray(data[start : start + size])))
        position = (start + size + 3) & ~3
    if position != len(data):
        raise ValueError("Unexpected UI resource block length")
    return result


def pack_blocks(items):
    result = bytearray(struct.pack("<I", len(items)))
    for kind, data in items:
        result.extend(struct.pack("<II", len(data), kind))
        result.extend(data)
        result.extend(bytes(-len(result) % 4))
    return bytes(result)


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
    def __init__(self, firmware):
        self.original = {}
        self.added = {}
        self.names = {}
        self.next_id = 0x0CF00000
        version, data_offset, count = struct.unpack_from("<III", firmware, BANK_OFFSET)
        if (version, data_offset, count) != (3, 0x176D0, 27):
            raise ValueError("Unexpected FW2.0.4 resource bank")
        for i in range(count):
            kind, entries, _, offset = struct.unpack_from(
                "<IIII", firmware, BANK_OFFSET + 12 + i * 16
            )
            kind = kind.to_bytes(4, "big").decode("ascii")
            for j in range(entries):
                resource_id, offset_data, size = struct.unpack_from(
                    "<III", firmware, BANK_OFFSET + offset + j * 12
                )
                start = BANK_OFFSET + data_offset + offset_data
                self.original[kind, resource_id] = firmware[start : start + size]

    def allocate(self, name=None):
        resource_id = self.next_id
        self.next_id += 1
        if any(key[1] == resource_id for key in self.original):
            raise ValueError("CFW resource ID overlaps a stock resource")
        if name:
            self.names[name] = resource_id
        return resource_id

    def add(self, kind, resource_id, data):
        key = (kind, resource_id)
        if key in self.added:
            raise ValueError(f"Duplicate CFW resource {key}")
        self.added[key] = bytes(data)

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
                    data = bytearray(self.original[key])
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


def create(firmware, text):
    resources = Resources(firmware)
    item_id = resources.allocate()
    screen_id = resources.allocate("CFW_Info_Screen")
    layout_id = resources.allocate("CFW_Info_Layout")
    preview_id = resources.allocate("CFW_Info_Preview")
    template_id = resources.allocate()
    title_source = resources.source("CFW Info")

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
        view = bytearray(prototype)
        view_id = resources.allocate()
        put(view, 0x0C, 15 if previous == 0 else 12)
        put(view, 0x10, previous)
        put(view, 0x14, 0 if previous == 0 else 3)
        put(view, 0x30, view_id)
        put(view, 0x64, resources.source(paragraph))
        views.append((kind, view))
        previous = view_id
    kind, prototype = legal_views[-1]
    margin = bytearray(prototype)
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

    # Keep the native Legal preview icon and give the preview its own title.
    preview_view_id = resources.allocate()
    preview = bytearray(resources.original["VLyt", LEGAL_PREVIEW_LAYOUT])
    if word(preview, 0x24) != 0x702:
        raise ValueError("Unexpected Legal preview title source")
    put(preview, 0x24, title_source)
    resources.add("VLyt", preview_view_id, preview)
    resources.add(
        "TEVT", preview_view_id, resources.original["TEVT", LEGAL_PREVIEW_LAYOUT]
    )
    resources.clone_layout(
        LEGAL_PREVIEW, preview_id, substitutions={LEGAL_PREVIEW_LAYOUT: preview_view_id}
    )
    layouts = blocks(resources.original["SLst", MAIN_SCREEN])
    prototype = next(
        (kind, data) for kind, data in layouts if word(data, 0) == LEGAL_PREVIEW
    )
    kind, data = prototype
    data = bytearray(data)
    put(data, 0, preview_id)
    layouts.append((kind, data))
    resources.add("SLst", MAIN_SCREEN, pack_blocks(layouts))

    items = blocks(resources.original["ITEM", 0x41])
    legal_index = next(
        i for i, (_, data) in enumerate(items) if word(data, 0x30) == LEGAL_ITEM
    )
    kind, data = items[legal_index]
    data = bytearray(data)
    put(data, 0x30, item_id)
    put(data, 0x68, title_source)
    items.insert(legal_index + 1, (kind, data))
    resources.add("ITEM", 0x41, pack_blocks(items))

    events = bytearray(resources.original["CEVT", MAIN_SCREEN])
    put(events, 0, word(events, 0) + 2)
    events.extend(
        menu_event(
            item_id,
            "chosen",
            "",
            "navigator.PushScreen",
            ["CFW_Info_Screen", "CFW_Info_Layout", "foregroundpush"],
        )
    )
    events.extend(
        menu_event(
            item_id,
            "delayedselected",
            "ShowSetting_Legal",
            "navigator.SwitchLayout",
            ["CFW_Info_Preview"],
        )
    )
    resources.add("CEVT", MAIN_SCREEN, events)
    return resources


def generate(firmware, directory, revision):
    import custom_eq_ui

    text_path = Path(__file__).resolve().parents[1] / "payload/cfw_info.txt"
    text = text_path.read_text().replace("{build_revision}", revision)
    resources = create(firmware, text)
    eq_definitions = custom_eq_ui.create(resources, firmware)
    lines = ['#include "resources.h"', '#include "custom_eq.h"', "", *eq_definitions]
    entries = []
    for i, ((kind, resource_id), data) in enumerate(resources.added.items()):
        lines.append(f"static const unsigned char resource_{i}[] = {{")
        for start in range(0, len(data), 16):
            lines.append(
                "    "
                + ", ".join(f"0x{byte:02x}" for byte in data[start : start + 16])
                + ","
            )
        lines.append("};")
        resource_type = int.from_bytes(kind.encode("ascii"), "big")
        entries.append(
            f"    {{{resource_type:#x}, {resource_id:#x}, sizeof(resource_{i}), resource_{i}}},"
        )
    lines += ["", "const struct cfw_resource cfw_resources[] = {", *entries, "};"]
    lines.append(f"const uint32_t cfw_resource_count = {len(entries)};")
    lines.append("const struct cfw_resource_name cfw_resource_names[] = {")
    for name, resource_id in resources.names.items():
        lines.append(f'    {{"{name}", {resource_id:#x}}},')
    lines += [
        "};",
        f"const uint32_t cfw_resource_name_count = {len(resources.names)};",
        "",
    ]
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / "cfw_info_resources.c"
    path.write_text("\n".join(lines))
    return path
