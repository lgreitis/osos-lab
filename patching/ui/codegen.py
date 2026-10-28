# SPDX-License-Identifier: GPL-3.0-only
"""Emit C source and headers from compiled resources and bindings."""

import json
import struct

from .resources import Resource


def emit_header(document, string_ids=None):
    if document.root:
        name = document.root.name
    elif document.music_entry:
        name = document.music_entry["id"]
    else:
        name = next(iter(document.strings), None) or document.events[0].handler
    prefix = name.lower()
    guard = name.upper() + "_UI_GENERATED_H"
    lines = [f"#ifndef {guard}", f"#define {guard}", '#include "ui.h"', "", "enum {"]
    lines.extend(f"    {item.name.upper()} = {item.slot}," for item in document.fields)
    lines += [
        f"    {name.upper()}_FIELDS = {len(document.fields)}",
        "};",
        "",
    ]
    if document.fields:
        lines.append(
            f"extern const struct cfw_ui_field {prefix}_fields[{len(document.fields)}];"
        )
    lines.extend(
        f"#define {name} {value:#x}u" for name, value in (string_ids or {}).items()
    )
    for item in document.actions:
        lines.append(f"extern const struct cfw_ui_action {item.name.lower()};")
    for value in document.values.values():
        lines.append(f"extern const int32_t {value.name}[{len(value.numbers)}];")
    return "\n".join([*lines, "", "#endif", ""])


def array(name, values, public=False, ctype="uint32_t"):
    prefix = "" if public else "static "
    text = ", ".join(str(value) if value < 0 else f"{value:#x}" for value in values)
    return f"{prefix}const {ctype} {name}[] = {{{text}}};"


def string_array(name, values):
    return (
        f"static const char *const {name}[] = {{"
        + ", ".join(json.dumps(v, ensure_ascii=False) for v in values)
        + "};"
    )


def emit_bindings(document, fields, actions):
    lines = []
    prefix = document.root.name.lower()
    for values in document.values.values():
        lines.append(array(values.name, values.numbers, public=True, ctype="int32_t"))
        lines.append(string_array(values.name + "_labels", values.labels))
    if fields:
        lines.append(f"extern const struct cfw_ui_model {prefix}_model;")
    for item in document.fields:
        labels = document.values[item.choices].labels
        size = (
            len(item.title.encode()) + 3 + max(len(label.encode()) for label in labels)
        )
        lines += [
            f"static char {prefix}_summary_{item.slot}[{size}];",
            f"static struct cfw_ui_field_state {prefix}_state_{item.slot} = {{.summary = {prefix}_summary_{item.slot}}};",
        ]
    for item in document.actions:
        binding = actions[item.name]
        labels = document.values[item.choices].labels
        name = item.name.lower()
        lines.append(f"static uint32_t {name}_selected_status;")
        lines.append(
            f"const struct cfw_ui_action {name} = {{"
            f"{binding.property:#x}, {len(labels)}, "
            f"{item.choices}_labels, &{name}_selected_status, {int(item.hide_when_zero)}}};"
        )
    if fields:
        lines.append(f"const struct cfw_ui_field {prefix}_fields[{len(fields)}] = {{")
        for item in document.fields:
            binding = fields[item.slot]
            values = document.values[item.choices]
            lines.append(
                "    {"
                + ", ".join(
                    [
                        f"{binding.property:#x}",
                        f"{binding.summary_property:#x}",
                        f"{binding.screen:#x}",
                        str(len(values.labels)),
                        str(item.default_index),
                        str(item.slot),
                        str(int(values.numeric)),
                        json.dumps(item.name),
                        json.dumps(item.title),
                        f"{item.choices}_labels",
                        f"&{prefix}_model",
                        f"&{prefix}_state_{item.slot}",
                    ]
                )
                + "},"
            )
        lines.append("};")
    return lines


def emit_models(documents):
    fields, actions = [], []
    for document in documents:
        if document.root:
            fields.extend(
                f"&{document.root.name.lower()}_fields[{item.slot}]"
                for item in document.fields
            )
            actions.extend(f"&{item.name.lower()}" for item in document.actions)
    return [
        "const struct cfw_ui_field *const cfw_ui_fields[] = {"
        + ", ".join(fields)
        + "};",
        f"const uint32_t cfw_ui_field_count = {len(fields)};",
        "const struct cfw_ui_action *const cfw_ui_actions[] = {"
        + ", ".join(actions)
        + "};",
        f"const uint32_t cfw_ui_action_count = {len(actions)};",
    ]


def emit_resource_data(name, data, *, mutable=False, aligned=False):
    data = Resource(data)
    qualifier = "" if mutable else "const "
    alignment = " __attribute__((aligned(4)))" if aligned else ""
    lines = [f"static {qualifier}unsigned char {name}[]{alignment} = {{"]
    for start in range(0, len(data), 16):
        lines.append(
            "    "
            + ", ".join(f"0x{byte:02x}" for byte in data.data[start : start + 16])
            + ","
        )
    lines.append("};")
    for destination, offset, size in data.copies():
        address = f"({offset:#x} + OSOS_RUNTIME_BASE - OSOS_LOAD_OFFSET)"
        lines.append(f"PATCH_COPY({name}, {destination}, {address}, {size});")
    return lines


def emit_resources(resources):
    lines = []
    entries = []
    for i, ((kind, resource_id), data) in enumerate(resources.added.items()):
        # Native BMap readers load header fields and pixels as halfwords/words.
        lines.extend(emit_resource_data(f"resource_{i}", data, aligned=kind == "BMap"))
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

    return lines


def emit_menu_items(resources):
    lines, entries = [], []
    for i, (table, after, kind, data) in enumerate(resources.menu_items):
        name = f"ui_menu_item_{i}"
        lines.extend(emit_resource_data(name, data, mutable=True, aligned=True))
        entries.append(
            f"    {{{table:#x}, {after:#x}, sizeof({name}), {kind:#x}, {name}}},"
        )
    return [
        *lines,
        "const struct cfw_ui_menu_item cfw_ui_menu_items[] = {",
        *entries,
        "};",
        f"const uint32_t cfw_ui_menu_item_count = {len(entries)};",
    ]


def emit_events(resources):
    lines = []
    entries = []
    for i, ((kind, resource_id), events) in enumerate(
        resources.event_extensions.items()
    ):
        data = struct.pack("<I", len(events)) + b"".join(events)
        lines.append(array(f"ui_events_{i}", data, ctype="unsigned char"))
        entries.append(
            f"    {{{int.from_bytes(kind.encode(), 'big'):#x}, {resource_id:#x}, "
            f"sizeof(ui_events_{i}), ui_events_{i}}},"
        )
    lines += [
        "const struct cfw_ui_events cfw_ui_events[] = {",
        *entries,
        "};",
        f"const uint32_t cfw_ui_event_count = {len(entries)};",
    ]
    lines.append("const struct cfw_ui_event_alias cfw_ui_event_aliases[] = {")
    for (kind, resource_id), original in resources.event_aliases.items():
        lines.append(
            f"    {{{int.from_bytes(kind.encode(), 'big'):#x}, {resource_id:#x}, {original:#x}}},"
        )
    lines += [
        "};",
        f"const uint32_t cfw_ui_event_alias_count = {len(resources.event_aliases)};",
    ]
    return lines


def emit_visibility(resources):
    lines, entries, keys = [], [], set()
    for i, entry in enumerate(resources.visibility):
        key = 2166136261
        for byte in entry["id"].encode():
            key = ((key ^ byte) * 16777619) & 0xFFFFFFFF
        if key in keys:
            raise ValueError("Duplicate menu visibility preference key")
        keys.add(key)
        lines.append(f"static uint32_t ui_visible_{i} = {entry['defaults']};")
        filename = f"iPod_Control\\Device\\CFWMenu_{key:08x}"
        items = ", ".join(f"{entry['items'][m]:#x}" for m in ("Main", "Music"))
        anchors = ", ".join(f"{entry['anchors'][m]:#x}" for m in ("Main", "Music"))
        entries.append(
            f"    {{{key:#x}, {entry['title']:#x}, {{{items}}}, {{{anchors}}}, "
            f"{entry['defaults']}, {json.dumps(filename)}, &ui_visible_{i}}},"
        )
    lines += [
        "const struct cfw_ui_visibility cfw_ui_visibility[] = {",
        *entries,
        "};",
        f"const uint32_t cfw_ui_visibility_count = {len(entries)};",
    ]
    return lines
