# SPDX-License-Identifier: GPL-3.0-only
"""Emit C source and headers from compiled resources and bindings."""

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
    if document.root and document.root.kind == "list":
        lines.append(f"#define {name.upper()}_CAPACITY {document.root.capacity}")
        lines.append(f"extern const struct cfw_ui_list {prefix};")
    for value in document.values.values():
        lines.append(f"extern const int32_t {value.name}[{len(value.numbers)}];")
    return "\n".join([*lines, "", "#endif", ""])


def array(name, values, public=False, ctype="uint32_t"):
    prefix = "" if public else "static "
    text = ", ".join(str(value) if value < 0 else f"{value:#x}" for value in values)
    return f"{prefix}const {ctype} {name}[] = {{{text}}};"


def emit_bindings(document, fields, actions, lists=None):
    lines = []
    prefix = document.root.name.lower()
    for name, (table, data, rows) in (lists or {}).items():
        name = name.lower()
        lines.extend(emit_data(name + "_template", data))
        for row in rows:
            lines.append(array(f"{name}_labels_{row.index}", row.labels))
        lines.append(f"static const struct cfw_ui_action {name}_rows[] = {{")
        for row in rows:
            lines.append(
                f"    {{{table:#x}, {row.index}, {row.item:#x}, 1, {name}_labels_{row.index}}},"
            )
        lines.append("};")
        lines.append(
            f"const struct cfw_ui_list {name} = {{{table:#x}, {len(rows)}, {name}_template, {name}_rows}};"
        )
    for slot, binding in fields.items():
        for key in ("labels", "marked", "summaries", "items"):
            lines.append(array(f"{prefix}_field_{slot}_{key}", getattr(binding, key)))

    for values in document.values.values():
        lines.append(array(values.name, values.numbers, public=True, ctype="int32_t"))

    for item in document.actions:
        binding = actions[item.name]
        labels = item.name.lower() + "_labels"
        lines.append(array(labels, binding.labels))
        lines.append(
            f"const struct cfw_ui_action {item.name.lower()} = {{"
            f"{binding.table:#x}, {binding.index}, {binding.item:#x}, "
            f"{len(binding.labels)}, {labels}}};"
        )

    if document.fields:
        lines.append(f"const struct cfw_ui_field {prefix}_fields[{len(fields)}] = {{")
        for slot in range(len(fields)):
            binding = fields[slot]
            values = [
                f"{value:#x}"
                for value in (
                    binding.parent_table,
                    binding.parent_index,
                    binding.parent_item,
                    binding.selector_table,
                    len(binding.labels),
                    binding.default,
                )
            ]
            arrays = [
                f"{prefix}_field_{slot}_{key}"
                for key in ("labels", "marked", "summaries", "items")
            ]
            lines.append("    {" + ", ".join(values + arrays) + "},")
        lines.append("};")

    return lines


def emit_data(name, data):
    data = Resource(data)
    lines = [f"static const unsigned char {name}[] = {{"]
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
        data = Resource(data)
        lines.extend(emit_data(f"resource_{i}", data))
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
