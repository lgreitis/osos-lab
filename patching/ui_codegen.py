# SPDX-License-Identifier: GPL-3.0-only
"""Emit C from constructed UI resources and bindings without allocating resources."""

from dataclasses import dataclass, field

from .resource import Resource


@dataclass
class FieldBinding:
    parent_item: int
    selector_table: int
    default: int
    labels: list[int]
    marked: list[int]
    summaries: list[int]
    items: list[int]
    parent_table: int = 0
    parent_index: int = 0


@dataclass
class ActionBinding:
    item: int
    index: int
    table: int = 0
    labels: list[int] = field(default_factory=list)


def array(name, values, public=False, ctype="uint32_t"):
    prefix = "" if public else "static "
    text = ", ".join(str(value) if value < 0 else f"{value:#x}" for value in values)
    return f"{prefix}const {ctype} {name}[] = {{{text}}};"


def emit_bindings(document, fields, actions):
    lines = []
    prefix = document.root.name.lower()
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


def emit_resources(resources):
    lines = []
    entries = []
    for i, ((kind, resource_id), data) in enumerate(resources.added.items()):
        data = Resource(data)
        lines.append(f"static const unsigned char resource_{i}[] = {{")
        for start in range(0, len(data), 16):
            lines.append(
                "    "
                + ", ".join(f"0x{byte:02x}" for byte in data.data[start : start + 16])
                + ","
            )
        lines.append("};")
        for destination, offset, size in data.copies():
            address = offset + 0x08000000 - 0xB6D8
            lines.append(
                f"PATCH_COPY(resource_{i}, {destination}, {address:#x}, {size});"
            )
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
