# SPDX-License-Identifier: GPL-3.0-only
"""Apple UI resource encoding, template provenance, and layout cloning."""

import struct
from dataclasses import dataclass


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

    def replace(self, kind, resource_id, data):
        key = (kind, resource_id)
        if key not in self.added and key not in self.original:
            raise ValueError(f"Cannot replace missing UI resource {key}")
        self.added[key] = Resource(data)

    def bind_screen_event(self, binding):
        encoded = (
            serialized_string(binding.event)
            + b"1"
            + serialized_string(binding.handler)
            + struct.pack("<I", 0)
        )
        for _, layout in blocks(self.original["SLst", binding.screen]):
            self.extend_events("SEVT", word(layout, 0), [encoded])

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

    def current(self, kind, resource_id):
        """Include earlier contributions when extending a shared native resource."""
        key = kind, resource_id
        return self.added[key] if key in self.added else self.original[key]

    def extend_events(self, kind, resource_id, events):
        data = Resource(self.current(kind, resource_id))
        put(data, 0, word(data, 0) + len(events))
        for event_data in events:
            data.extend(event_data)
        self.replace(kind, resource_id, data)

    def clone_layout(
        self,
        original_id,
        new_id,
        title_source=None,
        substitutions=None,
        item_table=None,
    ):
        layout = blocks(self.original["SLyt", original_id])
        for _, item in layout:
            instances = blocks(self.original["SSin", word(item, 0)])
            list_id = self.allocate()
            put(item, 0, list_id)
            for _, instance in instances:
                self.clone_instance(
                    instance, substitutions or {}, title_source, item_table
                )
            self.add("SSin", list_id, pack_blocks(instances))
        self.add("SLyt", new_id, pack_blocks(layout))
        self.add("SEVT", new_id, self.original["SEVT", original_id])

    def clone_instance(self, instance, substitutions, title_source, item_table):
        original_id = word(instance, 0)
        new_id = self.allocate()
        put(instance, 0, new_id)
        for offset in (4, 8):
            original = word(instance, offset)
            put(instance, offset, substitutions.get(original, original))
        for kind in ("VCrv", "VCvs"):
            key = kind, original_id
            if key not in self.original:
                continue
            data = Resource(self.original[key])
            if kind == "VCvs":
                data = self.rebind_view(data, instance, title_source, item_table)
            self.add(kind, new_id, data)

    def rebind_view(self, data, instance, title_source, item_table):
        if title_source is not None and word(instance, 4) == 0x0DAD016D:
            bindings = blocks(data)
            if len(bindings) != 1 or bindings[0][0] != 0x0DAD0172:
                raise ValueError("Unexpected Legal title binding")
            put(bindings[0][1], 12, title_source)
            data = pack_blocks(bindings)
        if item_table is not None:
            bindings = blocks(data)
            for kind, binding in bindings:
                if kind == 0x3F1:
                    put(binding, 4, item_table)
            data = pack_blocks(bindings)
        return data


def event(name, action):
    return (
        serialized_string(name)
        + b"1"
        + serialized_string("")
        + struct.pack("<I", 1)
        + serialized_string(action)
        + struct.pack("<I", 0)
    )
