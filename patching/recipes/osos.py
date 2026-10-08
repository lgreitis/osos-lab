# SPDX-License-Identifier: GPL-3.0-only
"""Compile the OSOS payload and resolve its patches into an assembly recipe."""

import json
import struct
from dataclasses import dataclass

from .. import declarations, ui
from ..declarations import Copy, Kind
from ..symbols import require
from ..toolchain import compile_payload
from .segments import Input, Recipe

BASE = 0x08000000
IMAGE_HEADER_SIZE = 0x800
IMAGE_SIZE_FIELDS = (0xC, 0x10, 0x14)


@dataclass(frozen=True)
class PayloadLayout:
    start: int
    end: int
    limit: int
    load_offset: int

    @property
    def reserved_size(self):
        return self.limit - self.start


def arm_call(address, destination):
    delta = destination - address - 8
    if address % 4 or destination % 4 or not -(1 << 25) <= delta < (1 << 25):
        raise ValueError("ARM call out of range or unaligned")
    return 0xEB000000 | ((delta >> 2) & 0xFFFFFF)


def payload_units(source, generated_ui):
    units = [
        (path.name, [])
        for path in sorted(source.iterdir())
        if path.suffix in (".c", ".S") and not path.name.endswith(".lds.S")
    ]
    return [*units, (str(generated_ui), [])]


def build_recipe(
    source,
    directory,
    fingerprint,
    templates,
    prefix,
    jobs,
    revision,
    version="0.0.0-dev",
):
    native = source / "native"
    bindings = json.loads((native / "ui.json").read_text())["bindings"]
    generated_ui = ui.generate(
        ui.Resources(templates, bindings),
        [
            source / name
            for name in (
                "cfw_info.ui",
                "custom_eq.ui",
                "play_next.ui",
                "song_info.ui",
                "album_artists.ui",
            )
        ],
        directory,
        revision,
        version,
    )
    units = payload_units(source, generated_ui)
    units.append((str(source / "patches/patches.c"), []))
    code, symbols = compile_payload(
        directory,
        source,
        units,
        "payload.lds.S",
        "payload",
        prefix,
        jobs,
        includes=(native, source / "patches"),
        thumb_symbols=True,
    )
    patches = declarations.collect(directory, units, prefix)
    return build(code, patches, symbols, fingerprint)


def payload_layout(code, symbols, original_size):
    layout = PayloadLayout(
        require(symbols, "__payload_start"),
        require(symbols, "__payload_end"),
        require(symbols, "__payload_limit"),
        require(symbols, "__osos_load_offset"),
    )
    if require(symbols, "__payload_file_offset") != original_size:
        raise ValueError("Payload file offset does not match OSOS")
    if (
        not layout.start <= layout.end <= layout.limit
        or len(code) > layout.reserved_size
    ):
        raise ValueError("Payload exceeds reserved memory")
    return layout


def resolve_copy(patch, symbols, layout):
    if (
        patch.offset < 0
        or patch.size <= 0
        or patch.offset + patch.size > patch.array_size
    ):
        raise ValueError(f"Copy exceeds array: {patch.symbol}")
    if patch.source < BASE:
        raise ValueError("Copy source outside native firmware")
    destination = require(symbols, patch.symbol) - layout.start + patch.offset
    source = Input("osos", patch.source - BASE + layout.load_offset, patch.size)
    return destination, source


def resolve_write(patch, symbols, layout):
    address = patch.address
    value = patch.value if patch.kind == Kind.WORD else require(symbols, patch.symbol)
    if address % 4:
        raise ValueError(f"Unaligned patch at {address:#x}")
    if patch.kind != Kind.WORD and (
        value % 4 or not layout.start <= value <= layout.limit
    ):
        raise ValueError(f"Patch symbol outside payload or unaligned: {patch.symbol}")
    if patch.kind in (Kind.CALL, Kind.JUMP) and value >= layout.end:
        raise ValueError(f"Hook outside payload: {patch.symbol}")
    if patch.kind == Kind.CALL:
        value = arm_call(address, value)
    after = struct.pack("<I", value)
    if patch.kind == Kind.JUMP:
        after = struct.pack("<II", 0xE51FF004, value)
    offset = address - BASE + layout.load_offset
    if offset < layout.load_offset or len(patch.expected) != len(after):
        raise ValueError(f"Invalid native patch at {address:#x}")
    return offset, after


def resolve_patches(recipe, patches, symbols, layout):
    writes, copies = [], []
    for patch in patches:
        if isinstance(patch, Copy):
            copies.append(resolve_copy(patch, symbols, layout))
        else:
            offset, after = resolve_write(patch, symbols, layout)
            recipe.expect("osos", offset, patch.expected)
            writes.append((offset, after))
    return writes, copies


def size_header_writes(recipe, original_size, output_size):
    before = struct.pack("<I", original_size - IMAGE_HEADER_SIZE)
    after = struct.pack("<I", output_size - IMAGE_HEADER_SIZE)
    writes = []
    for offset in IMAGE_SIZE_FIELDS:
        recipe.expect("osos", offset, before)
        writes.append((offset, after))
    return writes


def build(code, patches, symbols, fingerprint):
    original_size = fingerprint["bytes"]
    layout = payload_layout(code, symbols, original_size)
    recipe = Recipe({"osos": fingerprint})
    writes, copies = resolve_patches(recipe, patches, symbols, layout)
    writes.extend(
        size_header_writes(recipe, original_size, original_size + layout.reserved_size)
    )
    recipe.overlay(Input("osos", 0, original_size), writes)
    recipe.overlay(code, copies)
    recipe.zero(layout.reserved_size - len(code))
    return recipe
