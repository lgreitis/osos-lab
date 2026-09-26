# SPDX-License-Identifier: GPL-3.0-only
"""Construct assembly segments without loading the source firmware."""

import hashlib
import json
from dataclasses import dataclass

INTERFACE = "classic7g-file-v1"
MAX_OUTPUT = 0xC00000


def fingerprint(data):
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


@dataclass(frozen=True)
class Input:
    name: str
    offset: int
    size: int


class Recipe:
    def __init__(self, inputs):
        self.inputs = inputs
        self.segments = []
        self.checks = []
        self.relocations = {}
        self.ffs_checksums = []
        self.data = bytearray()
        self.size = 0

    def overlay(self, original, replacements):
        """Emit a source region or compiled image with nonoverlapping replacements."""
        size = original.size if isinstance(original, Input) else len(original)
        position = 0
        for offset, replacement in sorted(replacements, key=lambda span: span[0]):
            length = (
                replacement.size if isinstance(replacement, Input) else len(replacement)
            )
            if offset < position or length <= 0 or offset + length > size:
                raise ValueError(
                    f"Overlapping or out-of-range replacement at {offset:#x}"
                )
            self._region(original, position, offset)
            self._region(replacement, 0, length)
            position = offset + length
        self._region(original, position, size)

    def _region(self, part, start, end):
        if start == end:
            return
        if isinstance(part, Input):
            self.source(part.name, part.offset + start, end - start)
        else:
            self.literal(part[start:end])

    def relocate(self, name, base, count):
        """Source copies use relocated PE bytes; preimage checks use originals."""
        if name not in self.inputs or name in self.relocations:
            raise ValueError(f"Invalid or duplicate relocation input: {name}")
        if base < 0 or base + self.inputs[name]["bytes"] > 0x100000000 or count <= 0:
            raise ValueError(f"Invalid relocation layout: {name}")
        self.relocations[name] = {"base": base, "count": count}

    def _append(self, segment):
        size = segment["bytes"]
        if size <= 0 or self.size + size > MAX_OUTPUT:
            raise ValueError("Invalid recipe output size")
        if self.segments:
            previous = self.segments[-1]
            if (
                previous["kind"] == segment["kind"]
                and previous.get("name") == segment.get("name")
                and (
                    segment["kind"] == "zero"
                    or previous["offset"] + previous["bytes"] == segment["offset"]
                )
            ):
                previous["bytes"] += size
                self.size += size
                return
        self.segments.append(segment)
        self.size += size

    def source(self, name, offset, size):
        if (
            name not in self.inputs
            or offset < 0
            or size <= 0
            or offset + size > self.inputs[name]["bytes"]
        ):
            raise ValueError(f"Copy source outside input: {name} at {offset:#x}")
        self._append({"kind": "input", "name": name, "offset": offset, "bytes": size})

    def literal(self, data):
        # Emit long padding runs without storing them in the compiled-data asset.
        position = 0
        while position < len(data):
            zero = data.find(bytes(32), position)
            end = len(data) if zero < 0 else zero
            literal = data[position:end]
            if literal:
                self._append(
                    {"kind": "data", "offset": len(self.data), "bytes": len(literal)}
                )
                self.data.extend(literal)
            if zero < 0:
                break
            end = zero + 32
            while end < len(data) and data[end] == 0:
                end += 1
            self.zero(end - zero)
            position = end

    def zero(self, size):
        if size:
            self._append({"kind": "zero", "bytes": size})

    def expect(self, name, offset, data):
        if (
            name not in self.inputs
            or offset < 0
            or not data
            or offset + len(data) > self.inputs[name]["bytes"]
        ):
            raise ValueError("Expected bytes outside input")
        self.checks.append({"name": name, "offset": offset, "hex": data.hex()})

    def save(self, directory, name):
        if not self.segments:
            raise ValueError("Empty recipe")
        spec = {
            "schema": 2,
            "interface": INTERFACE,
            "inputs": self.inputs,
            "output": {"bytes": self.size},
            "data": fingerprint(self.data),
            "checks": self.checks,
            "relocations": self.relocations,
            "ffs_checksums": self.ffs_checksums,
            "segments": self.segments,
        }
        raw = json.dumps(spec, sort_keys=True, separators=(",", ":")) + "\n"
        if len(raw.encode()) > 2 * 1024 * 1024:
            raise ValueError("Recipe exceeds 2 MiB")
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / (name + ".json")
        data_path = directory / (name + ".data")
        path.write_text(raw)
        data_path.write_bytes(self.data)
        return {"recipe": str(path), "data": str(data_path)}
