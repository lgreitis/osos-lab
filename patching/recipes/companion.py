# SPDX-License-Identifier: GPL-3.0-only
"""Compile the Apple companion and assemble its loader, handoff, and helper regions."""

import json
from dataclasses import dataclass, field

from ..symbols import require
from ..toolchain import compile_payload
from . import native
from .segments import Input, Recipe

COMPANION_SIZE = 0x2B800


@dataclass
class LinkedImage:
    code: bytes
    symbols: dict[str, int]
    declarations: list[native.Declaration] = field(default_factory=list)


def compile_handoff(source, directory, prefix, jobs):
    directory = directory / "handoff"
    units = [
        (name, [])
        for name in (
            "../nor/s5l8702/handoff.S",
            "extension.c",
            "bds-combined.c",
            "../common/rom-context.c",
        )
    ]
    code, symbols = compile_payload(
        directory,
        source / "handoff",
        units,
        "../nor/s5l8702/handoff.lds.S",
        "extension",
        prefix,
        jobs,
        includes=(source, source / "nor", source.parents[1] / "payload"),
    )
    declarations = native.collect(directory, "extension", prefix)
    return LinkedImage(code, symbols, declarations)


def compile_helper(source, directory, prefix, jobs):
    code, symbols = compile_payload(
        directory / "native",
        source / "native",
        [(name, []) for name in ("resume.S", "../nor/s5l8702/dispatch.S", "hook.c")],
        "native.lds",
        "native",
        prefix,
        jobs,
        includes=(source, source / "nor", source.parents[1] / "payload"),
    )
    return LinkedImage(code, symbols)


def compile_startup(source, directory, prefix, jobs):
    directory = directory / "startup"
    units = [
        ("start.S", []),
        ("hook.c", []),
        ("../common/rom-context.c", ["-DSTARTUP_CONTEXT=1"]),
        ("../nor/s5l8702/modules.S", []),
        ("files.c", []),
        ("../nor/s5l8702/wrappers.S", []),
        ("../nor/s5l8702/patches.S", []),
    ]
    code, symbols = compile_payload(
        directory,
        source / "startup",
        units,
        "../nor/s5l8702/startup.lds.S",
        "probe",
        prefix,
        jobs,
        includes=(source, source / "nor", source.parents[1] / "payload"),
    )
    return LinkedImage(code, symbols, native.collect(directory, "probe", prefix))


def recipe_inputs(target_path, declarations):
    target = json.loads(target_path.read_text())["inputs"]
    filenames = {"apple_loader": "apple-loader.bin"}
    for declaration in declarations:
        name = declaration.name
        filenames[name.lower()] = {
            "osos": "osos.bin",
            "apple_loader": "apple-loader.bin",
        }.get(name, f"modules/{name}.pe32")
    return {name: target[path] for name, path in filenames.items()}


def append_loader(recipe, startup):
    base = require(startup.symbols, "apple_loader_base")
    loader_size = require(startup.symbols, "apple_loader_bytes")
    pad = require(startup.symbols, "image_start") - base
    pad_end = require(startup.symbols, "image_limit") - base
    if recipe.inputs["apple_loader"]["bytes"] != loader_size:
        raise ValueError("Unexpected Apple loader size")
    if not 0 < pad < pad_end <= loader_size:
        raise ValueError("Startup padding is outside the Apple loader")
    if not startup.code or len(startup.code) > pad_end - pad:
        raise ValueError("Startup code exceeds loader padding")
    copies, writes = native.apply(
        recipe,
        startup.code,
        startup.symbols,
        startup.declarations,
        "apple_loader",
        base,
    )
    recipe.overlay(Input("apple_loader", 0, pad), writes)
    recipe.overlay(startup.code, copies)
    remaining_offset = pad + len(startup.code)
    recipe.source("apple_loader", remaining_offset, loader_size - remaining_offset)


def append_extensions(recipe, handoff, helper):
    handoff_start = require(handoff.symbols, "image_start")
    handoff_limit = require(handoff.symbols, "image_limit")
    helper_start = require(helper.symbols, "image_start")
    helper_limit = require(helper.symbols, "image_limit")
    if (
        handoff_limit != helper_start
        or len(handoff.code) > handoff_limit - handoff_start
    ):
        raise ValueError("Extension overlaps native helper")
    if len(helper.code) > helper_limit - helper_start:
        raise ValueError("Native helper exceeds its reservation")
    copies, _ = native.apply(
        recipe, handoff.code, handoff.symbols, handoff.declarations
    )
    recipe.overlay(handoff.code, copies)
    recipe.zero(handoff_limit - handoff_start - len(handoff.code))
    recipe.literal(helper.code)
    recipe.zero(helper_limit - helper_start - len(helper.code))


def build_recipe(source, directory, target_path, prefix, jobs):
    handoff = compile_handoff(source, directory, prefix, jobs)
    helper = compile_helper(source, directory, prefix, jobs)
    startup = compile_startup(source, directory, prefix, jobs)
    inputs = recipe_inputs(target_path, handoff.declarations + startup.declarations)
    recipe = Recipe(inputs)
    append_loader(recipe, startup)
    append_extensions(recipe, handoff, helper)
    if recipe.size != COMPANION_SIZE:
        raise ValueError("Unexpected companion size")
    return recipe
