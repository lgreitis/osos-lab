# SPDX-License-Identifier: GPL-3.0-only
"""Editable UI bytes that retain references to their native templates."""

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
