# SPDX-License-Identifier: GPL-3.0-only
"""Translate linked copy, hook, and preimage declarations into recipe operations."""

import struct
from dataclasses import dataclass
from enum import IntEnum

from .recipe import Input
from .symbols import require
from .toolchain import run

RECORD = struct.Struct("<4I32s")


class Kind(IntEnum):
    MODULE = 1
    COPY = 2
    EXPECT = 3
    THUMB_CALL = 4
    ARM_JUMP = 5
    FFS_CHECKSUM = 6


@dataclass(frozen=True)
class Declaration:
    kind: Kind
    name: str
    address: int
    offset: int
    size: int
    expected: bytes = b""


def parse(data):
    declarations = []
    position = 0
    while position < len(data):
        if position + RECORD.size > len(data):
            raise ValueError("Truncated native declaration")

        kind, address, offset, size, name = RECORD.unpack_from(data, position)
        position += RECORD.size
        kind = Kind(kind)
        if not size or b"\0" not in name:
            raise ValueError("Invalid native declaration")
        name = name.split(b"\0", 1)[0].decode("ascii")
        if not name or not name.replace("_", "").isalnum():
            raise ValueError("Invalid native input name")

        expected = b""
        if kind == Kind.EXPECT:
            end = position + size
            expected = data[position:end]
            position = (end + 3) & ~3
            if position > len(data):
                raise ValueError("Truncated native preimage")

        declarations.append(Declaration(kind, name, address, offset, size, expected))

    return declarations


def collect(directory, output, prefix):
    path = directory / (output + ".native")
    # objcopy leaves the output untouched when the section is missing.
    path.write_bytes(b"")
    run(
        [
            prefix + "objcopy",
            "--dump-section",
            f".cfw.native={path}",
            output + ".elf",
        ],
        directory,
    )
    declarations = parse(path.read_bytes())
    if not declarations:
        raise ValueError(f"Missing native declarations in {output}")

    return declarations


def thumb_bl(source, target):
    delta = target - source - 4
    if source & 1 or target & 1 or not -(1 << 22) <= delta < (1 << 22):
        raise ValueError("Thumb branch out of range or unaligned")
    return struct.pack(
        "<HH", 0xF000 | ((delta >> 12) & 0x7FF), 0xF800 | ((delta >> 1) & 0x7FF)
    )


def arm_b(source, target):
    delta = target - source - 8
    if source % 4 or target % 4 or not -(1 << 25) <= delta < (1 << 25):
        raise ValueError("ARM branch out of range or unaligned")
    return struct.pack("<I", 0xEA000000 | ((delta >> 2) & 0xFFFFFF))


def apply(recipe, code, symbols, declarations, patch_input=None, patch_base=0):
    start = require(symbols, "image_start")
    replacements, writes = [], []
    for declaration in declarations:
        name = declaration.name.lower()
        offset = declaration.address - start
        if declaration.kind == Kind.EXPECT:
            recipe.expect(name, declaration.offset, declaration.expected)
            continue

        if declaration.kind == Kind.FFS_CHECKSUM:
            if name != patch_input or declaration.size != 1:
                raise ValueError("FFS checksum must belong to the patched image")
            recipe.ffs_checksums.append(declaration.offset)
            continue

        if declaration.kind in (Kind.THUMB_CALL, Kind.ARM_JUMP):
            if name != patch_input or declaration.size != 4:
                raise ValueError("Invalid native hook input or instruction size")
            target = declaration.address
            if declaration.kind == Kind.THUMB_CALL:
                target &= ~1
            if not start <= target < start + len(code):
                raise ValueError("Native hook target outside linked image")
            encode = thumb_bl if declaration.kind == Kind.THUMB_CALL else arm_b
            writes.append(
                (declaration.offset, encode(patch_base + declaration.offset, target))
            )
            continue

        if offset < 0 or offset + declaration.size > len(code):
            raise ValueError(f"Native slot outside linked image: {name}")
        if declaration.kind == Kind.MODULE:
            if declaration.size != recipe.inputs[name]["bytes"]:
                raise ValueError(f"Native module size differs from target: {name}")
            recipe.relocate(name, declaration.address, declaration.offset)
        else:
            if any(code[offset : offset + declaration.size]):
                raise ValueError(f"Native copy would overwrite compiled code: {name}")
            replacements.append(
                (offset, Input(name, declaration.offset, declaration.size))
            )

    return replacements, writes
