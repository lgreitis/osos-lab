#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate native bitmap metadata or compile a manifest into a disk theme."""

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from ui_metadata import Templates

from patching.theme_format import digest
from themes.compiler import compile_theme, describe, source_hash

ROOT = Path(__file__).resolve().parents[1]


def metadata(data):
    shim = json.loads((ROOT / "payload/native/ui.json").read_text())
    templates = Templates(data, shim["bank_offset"], shim["bank_header"])
    ids = {rid for kind, rid in templates if kind == "BMap"}
    names = {}
    relocation = 0x08000000 - 0xB798
    for at in range(0, len(data) - 8, 4):
        pointer, rid = struct.unpack_from("<II", data, at)
        offset = pointer - relocation
        if rid not in ids or not 0 <= offset < len(data):
            continue
        end = data.find(b"\0", offset, offset + 256)
        name = data[offset:end] if end >= 0 else b""
        if name and all(32 <= c < 127 for c in name):
            names.setdefault(rid, set()).add(name.decode())
    bitmaps = []
    for (kind, rid), (offset, size) in sorted(templates.index.items()):
        if kind != "BMap":
            continue
        candidates = names.get(rid, set())
        name = next(iter(candidates)) if len(candidates) == 1 else ""
        bitmaps.append(describe(rid, offset, data[offset : offset + size], name))
    return {
        "schema": 1,
        "input": {"bytes": len(data), "sha256": digest(data)},
        "bitmaps": bitmaps,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command",
        nargs="?",
        default="variants",
        choices=("metadata", "build", "variants"),
    )
    parser.add_argument("--name", default="Dark")
    parser.add_argument(
        "--accent", type=lambda value: int(value.removeprefix("#"), 16), default=0
    )
    parser.add_argument(
        "--input", type=Path, default=ROOT / "inputs/firmware-2.0.5/osos.bin"
    )
    parser.add_argument("--metadata", type=Path, default=ROOT / "firmware/bitmaps.json")
    parser.add_argument(
        "--manifest", type=Path, default=ROOT / "themes/dark/manifest.json"
    )
    parser.add_argument(
        "--out",
        type=Path,
        help="Output file for build, or output directory for variants (default: build/themes)",
    )
    args = parser.parse_args()
    try:
        data = args.input.read_bytes()
        if args.command == "metadata":
            catalog = metadata(data)
            pinned = json.loads((ROOT / "firmware/ui.json").read_text())["input"]
            if catalog["input"] != pinned:
                raise ValueError("Expected the pinned original OSOS image")
            args.metadata.write_text(json.dumps(catalog, indent=2) + "\n")
            print(f"Wrote {len(catalog['bitmaps'])} bitmap budgets: {args.metadata}")
        else:
            catalog = json.loads(args.metadata.read_text())
            variants = (
                json.loads((args.manifest.parent / "variants.json").read_text())
                if args.command == "variants"
                else [{"name": args.name, "accent": args.accent}]
            )
            for variant in variants:
                out = (
                    (args.out or ROOT / "build/themes") / variant["file"]
                    if args.command == "variants"
                    else args.out or ROOT / "build/themes/dark/theme.bin"
                )
                accent = variant["accent"]
                if isinstance(accent, str):
                    accent = int(accent.removeprefix("#"), 16)
                pack = compile_theme(
                    catalog, data, args.manifest, name=variant["name"], accent=accent
                )
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(pack)
                out.with_suffix(".json").write_text(
                    json.dumps(
                        {
                            "sources_sha256": source_hash(args.manifest),
                            "pack_sha256": digest(pack),
                        },
                        indent=2,
                    )
                    + "\n"
                )
                print(f"Theme: {len(pack):,} bytes, {out}")
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
