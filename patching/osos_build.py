# SPDX-License-Identifier: GPL-3.0-only
"""Compile feature sources and target metadata into an OSOS recipe."""

import json

from . import declarations, native_ui, osos
from .toolchain import compile_payload


def build_recipe(source, directory, target_path, prefix, jobs, revision):
    target = json.loads(target_path.read_text())
    fingerprint = target["inputs"]["osos.bin"]
    metadata = json.loads(
        target_path.with_name(target_path.stem + "-ui.json").read_text()
    )
    if metadata["schema"] != 1 or metadata["input"] != fingerprint:
        raise ValueError("UI metadata does not match the selected OSOS target")

    resources = native_ui.Resources(metadata["resources"])
    generated = native_ui.generate(
        resources, sorted(source.glob("*.ui")), directory, prefix, revision
    )

    units = [
        (path.name, [])
        for path in sorted(source.iterdir())
        if path.suffix in (".c", ".S") and not path.name.endswith(".lds.S")
    ]
    units.append((str(generated), []))
    code, linked = compile_payload(
        directory,
        source,
        units,
        "payload.lds.S",
        "payload",
        prefix,
        jobs,
        thumb_symbols=True,
    )

    patches = declarations.collect(directory, units, prefix)
    return osos.build(code, patches, linked, fingerprint)
