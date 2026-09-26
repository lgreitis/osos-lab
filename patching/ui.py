# SPDX-License-Identifier: GPL-3.0-only
"""Compile native UI declarations with the C preprocessor and validate their bindings."""

import re
import struct
import subprocess
from dataclasses import dataclass, field
from enum import IntEnum
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RECORD = struct.Struct("<6i64s96s64s")


class RecordKind(IntEnum):
    VERSION = 0
    VALUES = 1
    VALUE = 2
    RANGE = 3
    END_VALUES = 4
    MENU = 5
    END_MENU = 6
    SELECTOR = 7
    ACTION = 8
    SETTINGS_ENTRY = 9
    TEXT = 10
    STRING = 11


@dataclass
class Values:
    name: str
    numbers: list = field(default_factory=list)
    labels: list = field(default_factory=list)


@dataclass
class Item:
    kind: str
    name: str
    title: str
    choices: str = ""
    slot: int = 0
    default_index: int = 0
    text_file: str = ""
    items: list = field(default_factory=list)


@dataclass
class Document:
    root: Item
    values: dict
    fields: list
    actions: list
    after: int = 0
    open_action: str = ""
    strings: dict = field(default_factory=dict)


def parse(data):
    if not data or len(data) % RECORD.size:
        raise ValueError("Truncated UI metadata")

    values, stack, fields, actions, names = {}, [], [], [], set()
    root = group = entry = None
    strings_by_name = {}
    for index, raw in enumerate(RECORD.iter_unpack(data)):
        tag, *numbers = raw[:6]
        try:
            kind = RecordKind(tag)
        except ValueError:
            raise ValueError(f"Unknown UI record kind: {tag}") from None

        strings = []
        for value in raw[6:]:
            if b"\0" not in value:
                raise ValueError("UI identifier or text exceeds record capacity")
            strings.append(value.split(b"\0", 1)[0].decode("utf-8"))
        name, text, ref = strings
        if index == 0:
            if kind != RecordKind.VERSION or numbers[0] != 1:
                raise ValueError("Unsupported UI metadata version")
            continue

        if kind in (
            RecordKind.VALUES,
            RecordKind.RANGE,
            RecordKind.MENU,
            RecordKind.SELECTOR,
            RecordKind.ACTION,
            RecordKind.TEXT,
            RecordKind.STRING,
        ):
            if (
                not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name)
                or name.upper() in names
            ):
                raise ValueError(f"Invalid or duplicate UI name: {name}")
            names.add(name.upper())

        if group is not None and kind not in (RecordKind.VALUE, RecordKind.END_VALUES):
            raise ValueError("Unclosed UI values")

        if kind in (RecordKind.VALUES, RecordKind.RANGE):
            if stack or root is not None:
                raise ValueError("Declare UI values before menus")
            values[name] = value = Values(name)
            if kind == RecordKind.VALUES:
                group = value
            else:
                first, last, step, scale, sign = numbers
                if (
                    not step
                    or (last - first) * step < 0
                    or scale not in (1, 10, 100, 1000)
                    or sign not in (0, 1)
                    or (last - first) % step
                ):
                    raise ValueError(f"Invalid UI range: {name}")
                count = (last - first) // step + 1
                if count > 100:
                    raise ValueError("UI selectors support at most 100 choices")
                digits = len(str(scale)) - 1
                for number in range(first, last + (1 if step > 0 else -1), step):
                    prefix = "-" if number < 0 else "+" if sign and number else ""
                    label = str(abs(number) // scale)
                    if digits:
                        label += f".{abs(number) % scale:0{digits}d}"
                    value.numbers.append(number)
                    value.labels.append(prefix + label + text)
        elif kind == RecordKind.VALUE:
            if group is None:
                raise ValueError("UI_VALUE outside UI_VALUES")
            group.numbers.append(numbers[0])
            group.labels.append(text)
        elif kind == RecordKind.END_VALUES:
            if group is None:
                raise ValueError("UI_END_VALUES without UI_VALUES")
            group = None
        elif kind == RecordKind.MENU:
            item = Item("menu", name, text)
            if stack:
                stack[-1].items.append(item)
            elif root is None:
                root = item
            else:
                raise ValueError("Expected one root menu per UI declaration")
            stack.append(item)
        elif kind == RecordKind.END_MENU:
            if not stack or not stack[-1].items:
                raise ValueError("Empty or unmatched UI menu")
            stack.pop()
        elif kind in (RecordKind.SELECTOR, RecordKind.ACTION):
            if not stack or ref not in values:
                raise ValueError(f"Unknown values or missing menu for {name}: {ref}")
            item = Item(
                "selector" if kind == RecordKind.SELECTOR else "action",
                name,
                text,
                choices=ref,
                slot=numbers[0],
            )
            if kind == RecordKind.SELECTOR:
                if not 0 <= item.slot < 100 or numbers[1] not in values[ref].numbers:
                    raise ValueError(f"Invalid settings slot or default: {name}")
                item.default_index = values[ref].numbers.index(numbers[1])
                fields.append(item)
            else:
                actions.append(item)
            stack[-1].items.append(item)
        elif kind == RecordKind.SETTINGS_ENTRY:
            if stack or root is None or name != root.name or entry is not None:
                raise ValueError("Invalid Settings menu entry")
            entry = (numbers[0], ref)
        elif kind == RecordKind.TEXT:
            if root is not None or stack:
                raise ValueError("Expected one root page per UI declaration")
            root = Item("text", name, text, text_file=ref)
        elif kind == RecordKind.STRING:
            if stack:
                raise ValueError("Declare strings outside menus")
            strings_by_name[name] = text
        else:
            raise ValueError(f"Unknown UI record kind: {kind}")

    if root is None or stack or group is not None:
        raise ValueError("Incomplete UI declaration")
    for value in values.values():
        if not 1 <= len(value.labels) <= 100:
            raise ValueError(f"Expected 1..100 choices: {value.name}")
        if len(set(value.numbers)) != len(value.numbers):
            raise ValueError(f"Duplicate UI choice value: {value.name}")

    fields.sort(key=lambda item: item.slot)
    if [item.slot for item in fields] != list(range(len(fields))):
        raise ValueError("Settings slots must be unique and contiguous from zero")
    return Document(root, values, fields, actions, *(entry or (0, "")), strings_by_name)


def header(document, string_ids=None):
    prefix = document.root.name.lower()
    guard = document.root.name.upper() + "_UI_GENERATED_H"
    lines = [f"#ifndef {guard}", f"#define {guard}", '#include "ui.h"', "", "enum {"]
    lines.extend(f"    {item.name.upper()} = {item.slot}," for item in document.fields)
    lines += [
        f"    {document.root.name.upper()}_FIELDS = {len(document.fields)}",
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


def compile(source, directory, prefix):
    source, directory = Path(source).resolve(), Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    wrapper = directory / (source.stem + "_declarations.c")
    wrapper.write_text(
        '#include "ui_macros.h"\n'
        'const struct ui_record declarations[] __attribute__((section(".cfw_ui"), used)) = {\n'
        '    {0, {1}, "", "", ""},\n'
        f'#include "{source.name}"\n}};\n'
    )
    obj, binary = wrapper.with_suffix(".o"), wrapper.with_suffix(".bin")
    commands = [
        [
            prefix + "gcc",
            "-mcpu=arm926ej-s",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(source.parent),
            "-I",
            str(ROOT / "payload"),
            "-c",
            str(wrapper),
            "-o",
            str(obj),
        ],
        [prefix + "objcopy", "-O", "binary", "-j", ".cfw_ui", str(obj), str(binary)],
    ]
    for command in commands:
        subprocess.run(command, check=True)
    return parse(binary.read_bytes())
