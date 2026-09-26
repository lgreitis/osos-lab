# SPDX-License-Identifier: GPL-3.0-only
"""Build the Apple companion recipe without embedding Apple firmware."""

import json
from functools import partial

from . import native
from .recipe import Input, Recipe
from .symbols import require
from .toolchain import compile_payload

BASE = 0x22000000
PAD = 0x1A730
PAD_END = 0x1F510
LOADER_SIZE = 0x1F800


def build_recipe(source, directory, target_path, prefix, jobs):
    compile_image = partial(
        compile_payload, prefix=prefix, jobs=jobs, includes=(source,)
    )
    extension, extension_symbols = compile_image(
        directory / "handoff",
        source / "handoff",
        [
            (name, [])
            for name in (
                "extension.S",
                "extension.c",
                "bds-combined.c",
                "../common/rom-context.c",
            )
        ],
        "extension.lds",
        "extension",
    )
    helper, helper_symbols = compile_image(
        directory / "native",
        source / "native",
        [(name, []) for name in ("resume.S", "trampoline.S", "hook.c")],
        "native.lds",
        "native",
    )
    startup = directory / "startup"
    code, symbols = compile_image(
        startup,
        source / "startup",
        [
            ("start.S", []),
            ("hook.c", []),
            ("../common/rom-context.c", ["-DSTARTUP_CONTEXT=1"]),
            ("rom-payload.S", []),
            ("files.c", []),
            ("wrappers.S", []),
            ("patches.S", []),
        ],
        "probe.lds",
        "probe",
    )

    extension_declarations = native.collect(directory / "handoff", "extension", prefix)
    startup_declarations = native.collect(startup, "probe", prefix)

    target = json.loads(target_path.read_text())["inputs"]
    filenames = {"apple_loader": "apple-loader.bin"}
    for declaration in extension_declarations + startup_declarations:
        name = declaration.name
        filenames[name.lower()] = {
            "osos": "osos.bin",
            "apple_loader": "apple-loader.bin",
        }.get(name, f"modules/{name}.pe32")

    recipe = Recipe({name: target[path] for name, path in filenames.items()})
    if recipe.inputs["apple_loader"]["bytes"] != LOADER_SIZE:
        raise ValueError("Unexpected Apple loader size")
    if require(symbols, "image_start") != BASE + PAD:
        raise ValueError("Startup image does not begin in loader padding")
    if not code or len(code) > PAD_END - PAD:
        raise ValueError("Startup code exceeds loader padding")

    copies, writes = native.apply(
        recipe, code, symbols, startup_declarations, "apple_loader", BASE
    )
    recipe.overlay(Input("apple_loader", 0, PAD), writes)
    recipe.overlay(code, copies)
    recipe.source("apple_loader", PAD + len(code), LOADER_SIZE - PAD - len(code))

    extension_start = require(extension_symbols, "image_start")
    extension_limit = require(extension_symbols, "image_limit")
    helper_start = require(helper_symbols, "image_start")
    helper_limit = require(helper_symbols, "image_limit")
    if (
        extension_limit != helper_start
        or len(extension) > extension_limit - extension_start
    ):
        raise ValueError("Extension overlaps native helper")
    if len(helper) > helper_limit - helper_start:
        raise ValueError("Native helper exceeds its reservation")

    copies, _ = native.apply(
        recipe, extension, extension_symbols, extension_declarations
    )
    recipe.overlay(extension, copies)
    recipe.zero(extension_limit - extension_start - len(extension))
    recipe.literal(helper)
    recipe.zero(helper_limit - helper_start - len(helper))
    if recipe.size != 0x2B800:
        raise ValueError("Unexpected companion size")
    return recipe
