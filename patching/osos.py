# SPDX-License-Identifier: GPL-3.0-only
"""Resolve OSOS declarations into a recipe for the shared Rust assembler."""

import struct

from .declarations import Copy, Kind
from .recipe import Input, Recipe
from .symbols import require

BASE = 0x08000000
LOAD_OFFSET = 0xB6D8


def arm_call(address, destination):
    delta = destination - address - 8
    if address % 4 or destination % 4 or not -(1 << 25) <= delta < (1 << 25):
        raise ValueError("ARM call out of range or unaligned")
    return 0xEB000000 | ((delta >> 2) & 0xFFFFFF)


def build(code, patches, symbols, fingerprint):
    base = require(symbols, "__payload_start")
    limit = require(symbols, "__payload_limit")
    end = require(symbols, "__payload_end")
    original_size = fingerprint["bytes"]
    if require(symbols, "__payload_file_offset") != original_size:
        raise ValueError("Payload file offset does not match OSOS")
    if not base <= end <= limit or len(code) > limit - base:
        raise ValueError("Payload exceeds reserved memory")

    recipe = Recipe({"osos": fingerprint})
    writes, copies = [], []
    for patch in patches:
        if isinstance(patch, Copy):
            if (
                patch.offset < 0
                or patch.size <= 0
                or patch.offset + patch.size > patch.array_size
            ):
                raise ValueError(f"Copy exceeds array: {patch.symbol}")
            if patch.source < BASE:
                raise ValueError("Copy source outside native firmware")
            destination = require(symbols, patch.symbol) - base + patch.offset
            copies.append(
                (
                    destination,
                    Input("osos", patch.source - BASE + LOAD_OFFSET, patch.size),
                )
            )
            continue

        address = patch.address
        value = (
            patch.value if patch.kind == Kind.WORD else require(symbols, patch.symbol)
        )
        if address % 4:
            raise ValueError(f"Unaligned patch at {address:#x}")
        if patch.kind != Kind.WORD and (value % 4 or not base <= value <= limit):
            raise ValueError(
                f"Patch symbol outside payload or unaligned: {patch.symbol}"
            )
        if patch.kind in (Kind.CALL, Kind.JUMP) and value >= end:
            raise ValueError(f"Hook outside payload: {patch.symbol}")
        if patch.kind == Kind.CALL:
            value = arm_call(address, value)
        after = struct.pack("<I", value)
        if patch.kind == Kind.JUMP:
            after = struct.pack("<II", 0xE51FF004, value)
        offset = address - BASE + LOAD_OFFSET
        if offset < LOAD_OFFSET or len(patch.expected) != len(after):
            raise ValueError(f"Invalid native patch at {address:#x}")
        recipe.expect("osos", offset, patch.expected)
        writes.append((offset, after))

    output_size = original_size + limit - base
    for offset in (0xC, 0x10, 0x14):
        recipe.expect("osos", offset, struct.pack("<I", original_size - 0x800))
        writes.append((offset, struct.pack("<I", output_size - 0x800)))

    recipe.overlay(Input("osos", 0, original_size), writes)
    recipe.overlay(code, copies)
    recipe.zero(limit - base - len(code))
    return recipe
