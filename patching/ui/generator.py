# SPDX-License-Identifier: GPL-3.0-only
"""Read UI sources and write generated C resources and headers."""

from ..bitmap import encode_bmap, read_png
from .codegen import (
    emit_bindings,
    emit_events,
    emit_header,
    emit_menu_items,
    emit_models,
    emit_resources,
    emit_visibility,
)
from .compiler import compile_document
from .parser import read


def page_text(source, page, revision, version):
    text = (source.parent / page.text_file).read_text()
    return build_text(text, revision, version)


def build_text(text, revision, version):
    display = "Development" if version.startswith("0.0.0-dev") else version
    return text.replace("{build_revision}", revision).replace(
        "{build_version}", display
    )


def generate(resources, sources, directory, revision, version="0.0.0-dev"):
    directory.mkdir(parents=True, exist_ok=True)
    lines = [
        '#include "resources.h"',
        '#include "ui.h"',
        '#include "ui_visibility.h"',
        '#include "patch.h"',
        '#include "osos.h"',
        "",
    ]
    documents = []
    for source in sources:
        document = read(source)
        documents.append(document)
        text = ""
        if document.root and document.root.kind == "text":
            text = page_text(source, document.root, revision, version)
            document.strings = {
                name: build_text(value, revision, version)
                for name, value in document.strings.items()
            }
            document.translations = {
                name: {
                    language: build_text(value, revision, version)
                    for language, value in variants.items()
                }
                for name, variants in document.translations.items()
            }
        bitmap_ids = {}
        for name, (filename, bitmap_format) in document.bitmaps.items():
            bitmap = encode_bmap(
                *read_png(source.parent / filename), gray8=bitmap_format == "gray8"
            )
            bitmap_ids[name] = resource_id = resources.allocate(name)
            resources.add("BMap", resource_id, bitmap)
        try:
            compiled = compile_document(resources, document, text)
        except ValueError as error:
            raise ValueError(f"{source}: {error}") from error
        compiled.string_ids.update(bitmap_ids)
        if document.root:
            lines.extend(emit_bindings(document, compiled.fields, compiled.actions))
        (directory / (source.stem + "_ui.h")).write_text(
            emit_header(document, compiled.string_ids)
        )
    lines.extend(emit_resources(resources))
    lines.extend(emit_menu_items(resources))
    lines.extend(emit_events(resources))
    lines.extend(emit_models(documents))
    lines.extend(emit_visibility(resources))
    path = directory / "ui_resources.c"
    path.write_text("\n".join(lines))
    return path
