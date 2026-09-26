# SPDX-License-Identifier: GPL-3.0-only
"""Decode build-only C records into firmware writes and array copies."""

import struct
import subprocess
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path

RECORD = struct.Struct("<5I64s")


class Kind(IntEnum):
    WORD = 1
    POINTER = 2
    CALL = 3
    JUMP = 4
    COPY = 5


@dataclass(frozen=True)
class Write:
    kind: Kind
    address: int
    expected: bytes
    value: int = 0
    symbol: str = ""


@dataclass(frozen=True)
class Copy:
    symbol: str
    offset: int
    array_size: int
    source: int
    size: int


def parse(data):
    if len(data) % RECORD.size:
        raise ValueError("Truncated patch metadata")

    patches = []
    for tag, address, expected, extra, value, raw in RECORD.iter_unpack(data):
        try:
            kind = Kind(tag)
        except ValueError:
            raise ValueError(f"Unknown patch kind: {tag}") from None
        if b"\0" not in raw:
            raise ValueError("Unterminated patch symbol")
        symbol = raw.split(b"\0", 1)[0].decode("ascii")
        if kind != Kind.WORD and not symbol:
            raise ValueError("Patch is missing its target symbol")
        if kind == Kind.COPY:
            patches.append(Copy(symbol, expected, extra, address, value))
        else:
            before = struct.pack("<I", expected)
            if kind == Kind.JUMP:
                before += struct.pack("<I", extra)
            patches.append(Write(kind, address, before, value, symbol))

    return patches


def collect(directory, units, prefix):
    data = bytearray()
    for name, _ in units:
        if not str(name).endswith(".c"):
            continue
        obj = directory / (Path(name).name + ".o")
        output = obj.with_suffix(".patches")
        subprocess.run(
            [
                prefix + "objcopy",
                "-O",
                "binary",
                "-j",
                ".cfw.patch",
                str(obj),
                str(output),
            ],
            check=True,
        )
        data.extend(output.read_bytes())
    (directory / "patches.bin").write_bytes(data)
    return parse(data)
