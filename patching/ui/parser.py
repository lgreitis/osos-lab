# SPDX-License-Identifier: GPL-3.0-only
"""Parse XML declarations into validated UI documents."""

import re
import xml.etree.ElementTree as ET
from pathlib import Path

from .model import Document, Item, ScreenEvent, Values


def attributes(node, required="", optional="", children=()):
    required, optional = set(required.split()), set(optional.split())
    if required - node.attrib.keys() or node.attrib.keys() - required - optional:
        raise ValueError(f"Invalid attributes on <{node.tag}>")
    if (node.text or "").strip() or any(
        child.tag not in children or (child.tail or "").strip() for child in node
    ):
        raise ValueError(f"Invalid content in <{node.tag}>")


def integer(node, key, default=None, minimum=-(1 << 31), maximum=(1 << 31) - 1):
    text = node.get(key, default)
    value = int(text, 16 if text.lower().startswith("0x") else 10)
    if not minimum <= value <= maximum:
        raise ValueError(f"<{node.tag}> {key} is out of range")
    return value


def range_label(number, scale, signed, suffix):
    prefix = ""
    if number < 0:
        prefix = "-"
    elif signed and number:
        prefix = "+"
    digits = len(str(scale)) - 1
    label = str(abs(number) // scale)
    if digits:
        label += f".{abs(number) % scale:0{digits}d}"
    return prefix + label + suffix


def validate_values(value):
    if not 1 <= len(value.labels) <= 100:
        raise ValueError(f"Expected 1..100 choices: {value.name}")
    if len(set(value.numbers)) != len(value.numbers):
        raise ValueError(f"Duplicate UI choice value: {value.name}")


def validate_slots(fields):
    fields.sort(key=lambda item: item.slot)
    if [item.slot for item in fields] != list(range(len(fields))):
        raise ValueError("Settings slots must be unique and contiguous from zero")


class Parser:
    """Track document-wide names and bindings while reading nested menu nodes."""

    def __init__(self):
        self.document = Document(None, {}, [], [])
        self.names = set()
        self.entry = None

    def parse(self, tree):
        if tree.tag != "ui":
            raise ValueError("Expected <ui> root")
        attributes(
            tree,
            children=(
                "values",
                "range",
                "menu",
                "text",
                "toggle",
                "settings-entry",
                "string",
                "screen-event",
                "music-entry",
            ),
        )
        for node in tree:
            self.parse_declaration(node)
        self.apply_settings_entry()
        document = self.document
        if (
            document.root is None
            and not document.strings
            and not document.events
            and document.music_entry is None
        ):
            raise ValueError("Incomplete UI declaration")
        validate_slots(document.fields)
        return document

    def identifier(self, node):
        name = node.attrib["id"]
        if (
            not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name)
            or name.upper() in self.names
        ):
            raise ValueError(f"Invalid or duplicate UI name: {name}")
        self.names.add(name.upper())
        return name

    def parse_declaration(self, node):
        document = self.document
        if node.tag in ("values", "range"):
            value = (
                self.parse_values(node)
                if node.tag == "values"
                else self.parse_range(node)
            )
            validate_values(value)
            document.values[value.name] = value
        elif node.tag in ("menu", "text", "toggle"):
            if document.root is not None:
                raise ValueError("Expected one root menu or page per UI declaration")
            if node.tag == "toggle":
                attributes(node, "id title")
                document.root = Item(
                    "toggle", self.identifier(node), node.attrib["title"]
                )
            else:
                document.root = (
                    self.parse_menu(node)
                    if node.tag == "menu"
                    else self.parse_text(node)
                )
        elif node.tag == "settings-entry":
            attributes(node, "menu after", "open")
            if self.entry is not None:
                raise ValueError("Duplicate Settings menu entry")
            self.entry = node
        elif node.tag == "string":
            attributes(node, "id text")
            document.strings[self.identifier(node)] = node.attrib["text"]
        elif node.tag == "screen-event":
            document.events.append(self.parse_screen_event(node))
        elif node.tag == "music-entry":
            attributes(node, "id title after screen layout open highlight")
            if document.music_entry is not None:
                raise ValueError("Duplicate Music menu entry")
            self.identifier(node)
            document.music_entry = dict(node.attrib)

    def parse_values(self, node):
        attributes(node, "id", children=("value",))
        value = Values(self.identifier(node))
        for child in node:
            attributes(child, "number label")
            value.numbers.append(integer(child, "number"))
            value.labels.append(child.attrib["label"])
        return value

    def parse_range(self, node):
        attributes(node, "id first last step", "scale suffix signed")
        value = Values(self.identifier(node))
        first, last, step = (integer(node, key) for key in ("first", "last", "step"))
        scale = integer(node, "scale", "1")
        signed = node.get("signed", "false")
        if (
            not step
            or (last - first) * step < 0
            or (last - first) % step
            or scale not in (1, 10, 100, 1000)
            or signed not in ("true", "false")
        ):
            raise ValueError(f"Invalid UI range: {value.name}")
        if (last - first) // step + 1 > 100:
            raise ValueError("UI selectors support at most 100 choices")
        value.numbers = list(range(first, last + (1 if step > 0 else -1), step))
        value.labels = [
            range_label(number, scale, signed == "true", node.get("suffix", ""))
            for number in value.numbers
        ]
        return value

    def parse_menu(self, node):
        attributes(node, "id title", children=("menu", "selector", "action", "info"))
        item = Item("menu", self.identifier(node), node.attrib["title"])
        for child in node:
            if child.tag == "menu":
                row = self.parse_menu(child)
            else:
                row = self.parse_row(child)
            item.items.append(row)
        if not item.items:
            raise ValueError(f"Empty UI menu: {item.name}")
        return item

    def parse_row(self, node):
        if node.tag == "info":
            attributes(node, "id title")
            return Item("info", self.identifier(node), node.attrib["title"])
        required = "id title values"
        if node.tag == "selector":
            required += " slot default"
        attributes(node, required)
        name, ref = self.identifier(node), node.attrib["values"]
        if ref not in self.document.values:
            raise ValueError(f"Unknown values for {name}: {ref}")
        row = Item(node.tag, name, node.attrib["title"], choices=ref)
        if node.tag == "selector":
            self.bind_selector(node, row)
        else:
            self.document.actions.append(row)
        return row

    def bind_selector(self, node, row):
        row.slot = integer(node, "slot", minimum=0, maximum=99)
        default = integer(node, "default")
        choices = self.document.values[row.choices].numbers
        if default not in choices:
            raise ValueError(f"Invalid default for {row.name}: {default}")
        row.default_index = choices.index(default)
        self.document.fields.append(row)

    def parse_text(self, node):
        attributes(node, "id title file")
        return Item(
            "text",
            self.identifier(node),
            node.attrib["title"],
            text_file=node.attrib["file"],
        )

    def parse_screen_event(self, node):
        attributes(node, "screen event handler")
        if not node.attrib["event"] or not node.attrib["handler"]:
            raise ValueError("Screen events require an event and handler")
        return ScreenEvent(
            resource_reference(node.attrib["screen"]),
            node.attrib["event"],
            node.attrib["handler"],
        )

    def apply_settings_entry(self):
        if self.entry is None:
            if self.document.root and self.document.root.kind == "toggle":
                raise ValueError("A toggle requires a Settings menu entry")
            return
        entry, document = self.entry, self.document
        if document.root is None or entry.attrib["menu"] != document.root.name:
            raise ValueError("Invalid Settings menu entry")
        document.after = resource_reference(entry.attrib["after"])
        document.open_action = entry.get("open", "")


def resource_reference(value):
    if value.startswith("0x") or value.isdecimal():
        number = int(value, 16 if value.startswith("0x") else 10)
        if 0 <= number <= 0xFFFFFFFF:
            return number
    elif re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value):
        return value
    raise ValueError(f"Invalid native resource reference: {value}")


def parse(data):
    try:
        tree = ET.fromstring(data)
    except ET.ParseError as error:
        raise ValueError(f"Invalid UI XML: {error}") from error
    return Parser().parse(tree)


def read(source):
    try:
        return parse(Path(source).read_text())
    except ValueError as error:
        raise ValueError(f"{source}: {error}") from error
