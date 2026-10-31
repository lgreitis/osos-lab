#!/usr/bin/env python3
"""Export language catalogs and compile external packs without rebuilding firmware."""

import argparse
import hashlib
import json
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from patching.language_format import encode_index
from patching.languages import (
    NATIVE_TAGS,
    add_native,
    compile_pack,
    export_catalog,
    read_source,
    validate_catalog,
)
from patching.recipes.osos import UI_SOURCES
from patching.ui import Resources, generate
from patching.ui.parser import read
from tools.ui_metadata import Templates

ROOT = Path(__file__).resolve().parents[1]
NATIVE_NAMES = (
    "English",
    "Japanese",
    "Czech",
    "Danish",
    "German",
    "Spanish",
    "French",
    "Greek",
    "Italian",
    "Hungarian",
    "Dutch",
    "Norwegian",
    "Portuguese",
    "Russian",
    "Polish",
    "Finnish",
    "Swedish",
    "Turkish",
    "Korean",
    "Simplified Chinese",
    "Traditional Chinese",
)


def legacy_sources(sources):
    languages = {}
    for source in sources:
        document = read(source)
        for name, variants in document.translations.items():
            for native, text in variants.items():
                if native == 0:
                    continue
                languages.setdefault(native, {})[f"cfw:{document.namespace}/{name}"] = (
                    text
                )
    return [
        (
            {
                "tag": NATIVE_TAGS[native],
                "name": NATIVE_NAMES[native],
                "english_name": NATIVE_NAMES[native],
                "native_index": native,
            },
            translations,
        )
        for native, translations in sorted(languages.items())
    ]


def source_tree(languages, out):
    for metadata, translations in languages:
        root = ET.Element(
            "language",
            {
                "tag": metadata["tag"],
                "name": metadata["name"],
                "english-name": metadata["english_name"],
            },
        )
        for key, text in sorted(translations.items()):
            ET.SubElement(root, "string", {"key": key, "text": text})
        ET.indent(root, space="  ")
        path = out / metadata["tag"] / "strings.lo"
        path.parent.mkdir(parents=True, exist_ok=True)
        ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)


def publish(catalog, languages, out):
    validate_catalog(catalog)
    packs = [
        (metadata, compile_pack(catalog, metadata, translations))
        for metadata, translations in languages
    ]
    index = encode_index(
        [metadata for metadata, _ in packs],
        bytes.fromhex(catalog["firmware"]["sha256"]),
        bytes.fromhex(catalog["sha256"]),
    )
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".languages-", dir=out) as temporary:
        staging = Path(temporary)
        for metadata, pack in packs:
            path = staging / metadata["tag"]
            path.write_bytes(pack)
            destination = out / metadata["tag"] / "strings.bin"
            destination.parent.mkdir(parents=True, exist_ok=True)
            path.replace(destination)
        (staging / "catalog.json").write_text(
            json.dumps(catalog, ensure_ascii=False, indent=2) + "\n"
        )
        (staging / "catalog.json").replace(out / "catalog.json")
        (staging / "index.bin").write_bytes(index)
        (staging / "index.bin").replace(out / "index.bin")
    return len(packs)


def native_catalog(catalog, path):
    firmware = path.read_bytes()
    validate_catalog(catalog)
    if catalog["firmware"] != {
        "bytes": len(firmware),
        "sha256": hashlib.sha256(firmware).hexdigest(),
    }:
        raise ValueError("Native firmware fingerprint mismatch")
    shim = json.loads((ROOT / "payload/native/ui.json").read_text())
    bank = Templates(firmware, shim["bank_offset"], shim["bank_header"])
    return add_native(catalog, firmware, bank.index)


def build_assets(cfw_catalog, input_path, out):
    catalog = native_catalog(json.loads(cfw_catalog.read_text()), input_path)
    sources = [ROOT / "payload" / name for name in UI_SOURCES]
    return publish(catalog, legacy_sources(sources), out)


def export(input_path, out, source_out, revision="development", version="0.0.0-dev"):
    metadata = json.loads((ROOT / "firmware/ui.json").read_text())
    shim = json.loads((ROOT / "payload/native/ui.json").read_text())
    resources = Resources(metadata["resources"], shim["bindings"])
    sources = [ROOT / "payload" / name for name in UI_SOURCES]
    with tempfile.TemporaryDirectory(prefix="language-export-") as temporary:
        generate(resources, sources, Path(temporary), revision, version)
    catalog = native_catalog(export_catalog(resources, metadata["input"]), input_path)
    languages = legacy_sources(sources)
    count = publish(catalog, languages, out)
    if source_out is not None:
        source_tree(languages, source_out)
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    exporter = commands.add_parser(
        "export", help="Export the pinned Apple and CFW catalog"
    )
    exporter.add_argument("--input", type=Path, required=True)
    exporter.add_argument("--out", type=Path, default=ROOT / "build/languages")
    exporter.add_argument("--source-out", type=Path)
    exporter.add_argument("--revision", default="development")
    exporter.add_argument("--version", default="0.0.0-dev")
    compiler = commands.add_parser(
        "build", help="Compile tag-based sources against an existing catalog"
    )
    compiler.add_argument("--catalog", type=Path, required=True)
    compiler.add_argument("--sources", type=Path, required=True)
    compiler.add_argument("--out", type=Path, default=ROOT / "build/languages")
    args = parser.parse_args()
    try:
        if args.command == "export":
            count = export(
                args.input, args.out, args.source_out, args.revision, args.version
            )
        else:
            catalog = json.loads(args.catalog.read_text())
            sources = sorted(args.sources.glob("*/strings.lo"))
            if not sources:
                raise ValueError("No language sources found")
            count = publish(catalog, [read_source(path) for path in sources], args.out)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    print(f"Built {count} language packs: {args.out}")


if __name__ == "__main__":
    main()
