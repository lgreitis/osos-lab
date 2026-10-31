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
    if not 1 <= len(value.labels) <= 1024:
        raise ValueError(f"Expected 1..1024 choices: {value.name}")
    if len(set(value.numbers)) != len(value.numbers):
        raise ValueError(f"Duplicate UI choice value: {value.name}")


def validate_slots(fields):
    fields.sort(key=lambda item: item.slot)
    if [item.slot for item in fields] != list(range(len(fields))):
        raise ValueError("Settings slots must be unique and contiguous from zero")


def format_signature(text):
    arguments = {}
    position = 0
    sequential = 0
    numbered = None
    while position < len(text):
        if text[position] != "%":
            position += 1
            continue
        if text[position : position + 2] == "%%":
            position += 2
            continue
        match = re.match(r"%(?:([1-8])\$)?([su])", text[position:])
        if match is None:
            raise ValueError(f"Invalid UI format placeholder: {text[position:]}")
        explicit, kind = match.groups()
        if numbered is not None and numbered != bool(explicit):
            raise ValueError("Cannot mix numbered and sequential UI placeholders")
        numbered = bool(explicit)
        sequential += 1
        index = int(explicit) if explicit else sequential
        if index > 8 or (index in arguments and arguments[index] != kind):
            raise ValueError("Invalid UI format argument types or count")
        arguments[index] = kind
        position += match.end()
    if set(arguments) != set(range(1, len(arguments) + 1)):
        raise ValueError("UI format arguments must be contiguous from one")
    return "".join(arguments[index] for index in range(1, len(arguments) + 1))


class Parser:
    """Track document-wide names and bindings while reading nested menu nodes."""

    def __init__(self):
        self.document = Document(None, {}, [], [])
        self.names = set()
        self.entry = None

    def parse(self, tree, locale=None):
        if tree.tag != "ui":
            raise ValueError("Expected <ui> root")
        attributes(
            tree,
            children=(
                "values",
                "range",
                "menu",
                "text",
                "canvas",
                "toggle",
                "settings-entry",
                "string",
                "format",
                "screen-event",
                "music-entry",
                "bitmap",
            ),
        )
        for node in tree:
            self.parse_declaration(node)
        if locale is not None:
            if locale.tag != "locale":
                raise ValueError("Expected <locale> root")
            attributes(locale, children=("string", "format"))
            for node in locale:
                self.parse_declaration(node)
        self.apply_settings_entry()
        document = self.document
        for name in document.translations:
            if name not in document.strings:
                raise ValueError(f"Missing default UI string: {name}")
            if name in document.formats:
                signature = format_signature(document.strings[name])
                for translated in document.translations[name].values():
                    if format_signature(translated) != signature:
                        raise ValueError(f"Mismatched UI format arguments: {name}")
                document.formats[name] = signature
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
        elif node.tag in ("menu", "text", "toggle", "canvas"):
            if document.root is not None:
                raise ValueError("Expected one root menu or page per UI declaration")
            if node.tag in ("toggle", "canvas"):
                attributes(node, "id title")
                document.root = Item(
                    node.tag, self.identifier(node), node.attrib["title"]
                )
            else:
                document.root = (
                    self.parse_menu(node)
                    if node.tag == "menu"
                    else self.parse_text(node)
                )
        elif node.tag == "settings-entry":
            attributes(node, "menu after", "open preview image caption")
            if self.entry is not None:
                raise ValueError("Duplicate Settings menu entry")
            self.entry = node
        elif node.tag == "bitmap":
            attributes(node, "id file", "format")
            bitmap_format = node.get("format", "auto")
            if bitmap_format not in ("auto", "gray8"):
                raise ValueError("Bitmap format must be auto or gray8")
            document.bitmaps[self.identifier(node)] = (
                node.attrib["file"],
                bitmap_format,
            )
        elif node.tag in ("string", "format"):
            attributes(node, "id text", "lang")
            name = node.attrib["id"]
            if name not in document.translations:
                self.identifier(node)
                document.translations[name] = {}
                if node.tag == "format":
                    document.formats[name] = ""
            elif (node.tag == "format") != (name in document.formats):
                raise ValueError(f"Cannot mix string and format declarations: {name}")
            if "lang" in node.attrib:
                language = integer(node, "lang", minimum=0, maximum=20)
                if language in document.translations[name]:
                    raise ValueError(f"Duplicate UI string language: {name}")
                document.translations[name][language] = node.attrib["text"]
            else:
                if name in document.strings:
                    raise ValueError(f"Duplicate default UI string: {name}")
                document.strings[name] = node.attrib["text"]
        elif node.tag == "screen-event":
            document.events.append(self.parse_screen_event(node))
        elif node.tag == "music-entry":
            attributes(
                node,
                "id title after screen layout open highlight",
                "main-after main-visible music-visible",
            )
            if document.music_entry is not None:
                raise ValueError("Duplicate Music menu entry")
            self.identifier(node)
            document.music_entry = dict(node.attrib)
            for attribute, default in (
                ("main-visible", "false"),
                ("music-visible", "true"),
            ):
                value = node.get(attribute, default)
                if value not in ("true", "false"):
                    raise ValueError(f"Invalid {attribute} on <music-entry>")
                document.music_entry[attribute] = value

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
        value.numeric = True
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
        if (last - first) // step + 1 > 1024:
            raise ValueError("UI selectors support at most 1024 choices")
        value.numbers = list(range(first, last + (1 if step > 0 else -1), step))
        suffix = node.get("suffix", "")
        if suffix.startswith("@"):
            value.suffix, suffix = suffix, ""
        value.labels = [
            range_label(number, scale, signed == "true", suffix)
            for number in value.numbers
        ]
        return value

    def parse_menu(self, node):
        attributes(
            node, "id title", children=("menu", "selector", "action", "info", "canvas")
        )
        item = Item("menu", self.identifier(node), node.attrib["title"])
        for child in node:
            if child.tag == "menu":
                row = self.parse_menu(child)
            else:
                row = self.parse_row(child)
            item.items.append(row)
        # Native VMax controls the row count only for the final dynamic item.
        if any(row.hide_when_zero for row in item.items[:-1]):
            raise ValueError("A hide-when-zero action must be the final menu row")
        if not item.items:
            raise ValueError(f"Empty UI menu: {item.name}")
        return item

    def parse_row(self, node):
        if node.tag == "canvas":
            attributes(node, "id title")
            return Item(node.tag, self.identifier(node), node.attrib["title"])
        if node.tag == "info":
            attributes(node, "id title", "unknown")
            return Item(
                "info",
                self.identifier(node),
                node.attrib["title"],
                unknown=node.get("unknown", "Unknown"),
            )
        required = "id title values"
        if node.tag == "selector":
            required += " slot default"
        attributes(node, required, "hide-when-zero" if node.tag == "action" else "")
        name, ref = self.identifier(node), node.attrib["values"]
        if ref not in self.document.values:
            raise ValueError(f"Unknown values for {name}: {ref}")
        row = Item(node.tag, name, node.attrib["title"], choices=ref)
        if node.tag == "selector":
            self.bind_selector(node, row)
        else:
            hidden = node.get("hide-when-zero", "false")
            if hidden not in ("true", "false"):
                raise ValueError("Invalid hide-when-zero on <action>")
            row.hide_when_zero = hidden == "true"
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


def parse(data, locale=None):
    try:
        tree = ET.fromstring(data)
        locale_tree = ET.fromstring(locale) if locale is not None else None
    except ET.ParseError as error:
        raise ValueError(f"Invalid UI XML: {error}") from error
    return Parser().parse(tree, locale_tree)


def read(source):
    source = Path(source)
    try:
        locale = source.with_suffix(".lo")
        document = parse(
            source.read_text(), locale.read_text() if locale.exists() else None
        )
        document.namespace = source.stem
        return document
    except ValueError as error:
        raise ValueError(f"{source} (locale {locale}): {error}") from error
