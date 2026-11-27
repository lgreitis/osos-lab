# SPDX-License-Identifier: GPL-3.0-only
"""Parse XML declarations into validated UI documents."""

import re
import xml.etree.ElementTree as ET
from pathlib import Path

from .model import Document, Item, ScreenEvent


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


class Parser:
    """Track document-wide names and bindings while reading nested menu nodes."""

    def __init__(self):
        self.document = Document(None)
        self.names = set()
        self.entry = None

    def parse(self, tree):
        if tree.tag != "ui":
            raise ValueError("Expected <ui> root")
        attributes(
            tree,
            children=(
                "text",
                "list",
                "toggle",
                "settings-entry",
                "string",
                "screen-event",
                "layout-event",
                "bitmap",
            ),
        )
        for node in tree:
            self.parse_declaration(node)
        self.apply_settings_entry()
        document = self.document
        if document.root is None and not document.strings and not document.events:
            raise ValueError("Incomplete UI declaration")
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
        if node.tag in ("text", "toggle", "list"):
            if document.root is not None:
                raise ValueError("Expected one root page per UI declaration")
            if node.tag in ("toggle", "list"):
                attributes(
                    node,
                    "id title",
                    "select layout" if node.tag == "list" else "",
                )
                document.root = Item(
                    node.tag, self.identifier(node), node.attrib["title"]
                )
                if node.tag == "list":
                    document.root.select = node.get("select", "return")
                    document.root.layout = node.get("layout", "inline")
                    if document.root.select not in ("return", "stay"):
                        raise ValueError("<list> select must be return or stay")
                    if document.root.layout not in ("inline", "stacked"):
                        raise ValueError("<list> layout must be inline or stacked")
            else:
                document.root = self.parse_text(node)
        elif node.tag == "settings-entry":
            attributes(node, "menu after", "open preview image caption")
            if self.entry is not None:
                raise ValueError("Duplicate Settings menu entry")
            self.entry = node
        elif node.tag == "string":
            attributes(node, "id text")
            document.strings[self.identifier(node)] = node.attrib["text"]
        elif node.tag == "bitmap":
            attributes(node, "id file", "format")
            bitmap_format = node.get("format", "auto")
            if bitmap_format not in ("auto", "gray8"):
                raise ValueError("Bitmap format must be auto or gray8")
            document.bitmaps[self.identifier(node)] = (
                node.attrib["file"],
                bitmap_format,
            )
        elif node.tag == "screen-event":
            document.events.append(self.parse_screen_event(node))
        elif node.tag == "layout-event":
            attributes(node, "layout event handler")
            if not node.attrib["event"] or not node.attrib["handler"]:
                raise ValueError("Layout events require an event and handler")
            document.events.append(
                ScreenEvent(
                    resource_reference(node.attrib["layout"]),
                    node.attrib["event"],
                    node.attrib["handler"],
                    "SEVT",
                )
            )

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
        document.preview = entry.get("preview", "")
        if document.preview not in ("", "image"):
            raise ValueError("Settings preview must be image")
        document.preview_image = entry.get("image", "")
        document.preview_caption = entry.get("caption", "")
        if (
            document.preview_image or document.preview_caption
        ) and document.preview != "image":
            raise ValueError("Settings image and caption require an image preview")
        if document.preview_image and document.preview_image not in document.bitmaps:
            raise ValueError("Settings preview image must reference a declared bitmap")


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
