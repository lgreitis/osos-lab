# SPDX-License-Identifier: GPL-3.0-only
"""Apple UI resource encoding, template provenance, and layout cloning."""

import struct
from dataclasses import dataclass

from .model import Text


@dataclass
class Template:
    offset: int
    size: int
    words: dict
    firmware: bytes = None

    def word(self, offset):
        if offset < 0 or offset + 4 > self.size:
            raise ValueError("Word outside UI template")
        if self.firmware is not None:
            self.words[offset] = struct.unpack_from(
                "<I", self.firmware, self.offset + offset
            )[0]
        if offset not in self.words:
            raise ValueError(
                f"Missing UI structure metadata at {self.offset + offset:#x}"
            )
        return self.words[offset]


class Resource:
    """Literal bytes plus per-byte template provenance; slices preserve provenance."""

    def __init__(self, data=b""):
        if isinstance(data, Resource):
            self.data = data.data.copy()
            self.origins = data.origins.copy()
        else:
            self.data = bytearray(data)
            self.origins = [None] * len(data)

    @classmethod
    def template(cls, template):
        result = cls(bytes(template.size))
        result.origins = [(template, i) for i in range(template.size)]
        return result

    def __len__(self):
        return len(self.data)

    def __getitem__(self, key):
        if not isinstance(key, slice) or key.step not in (None, 1):
            raise ValueError("UI resources require contiguous slices")
        result = Resource(self.data[key])
        result.origins = self.origins[key]
        return result

    def __setitem__(self, key, value):
        value = Resource(value)
        if len(self.data[key]) != len(value):
            raise ValueError("UI replacement changes template size")
        self.data[key] = value.data
        self.origins[key] = value.origins

    def extend(self, value):
        value = Resource(value)
        self.data.extend(value.data)
        self.origins.extend(value.origins)

    def word(self, offset):
        origins = self.origins[offset : offset + 4]
        if len(origins) != 4:
            raise ValueError("Truncated UI word")
        if all(origin is None for origin in origins):
            return struct.unpack_from("<I", self.data, offset)[0]

        first = origins[0]
        if first is None or any(
            origin is None or origin[0] is not first[0] or origin[1] != first[1] + i
            for i, origin in enumerate(origins)
        ):
            raise ValueError("UI word crosses template boundaries")
        return first[0].word(first[1])

    def copies(self):
        position = 0
        while position < len(self):
            origin = self.origins[position]
            if origin is None:
                position += 1
                continue

            template, source = origin
            end = position + 1
            while end < len(self):
                following = self.origins[end]
                if (
                    following is None
                    or following[0] is not template
                    or following[1] != source + end - position
                ):
                    break
                end += 1

            yield position, template.offset + source, end - position
            position = end


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


def event(name, action=None, *, handler="", arguments=()):
    return (
        serialized_string(name)
        + b"1"
        + serialized_string(handler)
        + struct.pack("<I", int(action is not None))
        + (
            serialized_string(action)
            + struct.pack("<I", len(arguments))
            + b"".join(serialized_string(argument) for argument in arguments)
            if action is not None
            else b""
        )
    )


def menu_event(item, name, handler, action=None, arguments=()):
    return event(
        f"list.pid.{item}.{name}", action, handler=handler, arguments=arguments
    )


class Resources:
    def __init__(self, templates, bindings=None):
        self.bindings = bindings or {}
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
        self.translations = {}
        self.strings = {}
        self.formats = {}
        self.names = {}
        self.event_aliases = {}
        self.event_extensions = {}
        self.menu_items = []
        self.visibility = []
        self.next_id = 0x0CF00000

    def native(self, name):
        return self.bindings[name] if isinstance(name, str) else name

    def stock(self, kind, name):
        return self.original[kind, self.native(name)]

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

    def replace(self, kind, resource_id, data):
        key = (kind, resource_id)
        if key not in self.added and key not in self.original:
            raise ValueError(f"Cannot replace missing UI resource {key}")
        self.added[key] = Resource(data)

    def alias_events(self, kind, resource_id, original_id):
        if (kind, original_id) not in self.added:
            self.stock(kind, original_id)
        self.event_aliases[kind, resource_id] = original_id

    def set_events(self, kind, resource_id, data):
        self.event_aliases.pop((kind, resource_id), None)
        for (other_kind, other_id), other in self.added.items():
            if other_kind == kind and other.data == data and not any(other.origins):
                self.alias_events(kind, resource_id, other_id)
                return
        self.add(kind, resource_id, data)

    def bind_screen_event(self, binding):
        encoded = event(binding.event, handler=binding.handler)
        self.extend_events("CEVT", self.native(binding.screen), [encoded])

    def source(self, text, string_id=None):
        if string_id is None:
            string_id = self.allocate()
        self.string(string_id, text)
        return self.property_source(0x80, string_id)

    def property_source(self, service, property_id):
        source_id = self.allocate()
        self.add(
            "SORC",
            source_id,
            pack_blocks([(0x534F5243, struct.pack("<III", service, property_id, 1))]),
        )
        return source_id

    def string(self, string_id, text):
        if isinstance(text, str):
            text = Text(text)
        self.add("Str ", string_id, text.default.encode("utf-8") + b"\0")
        self.strings[string_id] = text
        for language, translated in sorted(text.translations.items()):
            self.translations[string_id, language] = translated.encode("utf-8") + b"\0"

    def current(self, kind, resource_id):
        """Include earlier contributions when extending a shared native resource."""
        key = kind, resource_id
        return self.added[key] if key in self.added else self.original[key]

    def extend_events(self, kind, resource_id, events):
        if (kind, resource_id) not in self.added:
            self.stock(kind, resource_id)
        self.event_extensions.setdefault((kind, resource_id), []).extend(events)

    def insert_menu_item(self, table, after, item):
        rows = blocks(self.stock("ITEM", table))
        if not any(word(row, 0x30) == after for _, row in rows):
            raise ValueError(f"Menu anchor {after:#x} missing from {table:#x}")
        kind, data = item
        self.menu_items.append((table, after, kind, Resource(data)))

    def clone_layout(
        self,
        original_id,
        new_id,
        title_source=None,
        substitutions=None,
        item_table=None,
        view_bindings=None,
    ):
        layout = blocks(self.original["SLyt", original_id])
        for _, item in layout:
            instances = blocks(self.original["SSin", word(item, 0)])
            list_id = self.allocate()
            put(item, 0, list_id)
            for _, instance in instances:
                self.clone_instance(
                    instance,
                    substitutions or {},
                    title_source,
                    item_table,
                    view_bindings or {},
                )
            self.add("SSin", list_id, pack_blocks(instances))
        self.add("SLyt", new_id, pack_blocks(layout))
        self.alias_events("SEVT", new_id, original_id)

    def clone_instance(
        self, instance, substitutions, title_source, item_table, view_bindings
    ):
        original_id = word(instance, 0)
        template_id = word(instance, 4)
        original = self.original.get(("VCvs", original_id))
        binding = view_bindings.get(template_id)
        if binding is None and original is not None:
            binding = self.rebind_view(
                Resource(original), instance, title_source, item_table
            )
        for offset in (4, 8):
            value = word(instance, offset)
            if value in substitutions:
                put(instance, offset, substitutions[value])
        if binding is None or (
            original is not None
            and binding.data == original.data
            and binding.origins == original.origins
        ):
            return
        new_id = self.allocate()
        put(instance, 0, new_id)
        if ("VCrv", original_id) in self.original:
            self.add("VCrv", new_id, self.original["VCrv", original_id])
        self.add("VCvs", new_id, binding)

    def rebind_view(self, data, instance, title_source, item_table):
        titles = {
            self.native(template): self.native(view)
            for template, view in (
                ("StatusBarWhite_Template", "StatusBarWhite_Text"),
                ("NowPlaying_StatusBar_Template", "NowPlaying_StatusBar_Text_View"),
            )
            if template in self.bindings
        }
        title_view = titles.get(word(instance, 4))
        if title_source is not None and title_view is not None:
            bindings = blocks(data)
            for index, (view, binding) in enumerate(bindings):
                if view != title_view:
                    continue
                options = blocks(binding)
                if not any(kind == 0x534F5243 for kind, _ in options):
                    raise ValueError("Native title is missing its source binding")
                bindings[index] = (
                    view,
                    pack_blocks(
                        [
                            (
                                kind,
                                struct.pack("<I", title_source)
                                if kind == 0x534F5243
                                else value,
                            )
                            for kind, value in options
                        ]
                    ),
                )
                break
            else:
                raise ValueError("Native status bar is missing its title view")
            data = pack_blocks(bindings)
        if item_table is not None:
            bindings = blocks(data)
            changed = False
            for kind, binding in bindings:
                if kind == 0x3F1:
                    put(binding, 4, item_table)
                    changed = True
            if changed:
                data = pack_blocks(bindings)
        return data
