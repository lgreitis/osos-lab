# SPDX-License-Identifier: GPL-3.0-only
"""The bitmap assets of a built extension as JSON lines, for theme tooling:
what a theme must match to replace one."""

import argparse
import json
import struct
import sys
from pathlib import Path

import bitmap
import manifest
import rvx
import tomllib


def record(identifier, asset, bmap, menu, preload):
    """The catalog record of the BMap `bmap`: id, asset, menu, format, width,
    height, stride, depth, palette_colors (0 without a palette), bytes (header
    included), preload."""
    fmt, _, stride, depth, top, left, bottom, right, _ = bitmap.HEADER.unpack_from(bmap)
    palette_colors = 0
    if fmt == bitmap.INDEXED8:
        palette_colors = struct.unpack_from("<I", bmap, bitmap.HEADER.size)[0]
    return {
        "id": identifier,
        "asset": asset,
        "menu": menu,
        "format": fmt,
        "width": right - left,
        "height": bottom - top,
        "stride": stride,
        "depth": depth,
        "palette_colors": palette_colors,
        "bytes": len(bmap),
        "preload": preload,
    }


def catalog(toml_path, rvx_path):
    """One record per bitmap asset of the extension."""
    document = tomllib.loads(toml_path.read_text())
    declared = {a["name"]: a for a in manifest.assets(document)}
    by_hash = {rvx.fnv1a(name.encode()): name for name in declared}
    data = rvx_path.read_bytes()
    _, entries = rvx.unpack(data)
    records = []
    for entry in entries:
        if entry["kind"] != rvx.KIND_BITMAP:
            continue
        name = by_hash.get(entry["name"])
        if name is None:
            raise ValueError(
                f"{rvx_path}: bitmap {entry['name']:#x} is not in the manifest"
            )
        bmap = rvx.payload(data, entry)
        records.append(
            record(
                document["id"],
                name,
                bmap,
                declared[name]["pane"] == "icon",
                declared[name]["preload"],
            )
        )
    return records


def core_catalog(assets_dir):
    """The records of the core's own menu bitmaps, `core/assets/*.png`."""
    return [
        record(
            manifest.CORE_ID,
            png.stem,
            bitmap.png_to_bmap(png.read_bytes(), "icon"),
            True,
            "optional",
        )
        for png in sorted(Path(assets_dir).glob("*.png"))
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("toml", type=Path, nargs="?", metavar="manifest.toml")
    parser.add_argument("rvx", type=Path, nargs="?", metavar="file.rvx")
    parser.add_argument(
        "--core", type=Path, metavar="DIR", help="the core's assets folder"
    )
    args = parser.parse_args()
    if bool(args.core) == bool(args.toml and args.rvx):
        parser.error("give manifest.toml and file.rvx, or --core DIR")
    try:
        records = core_catalog(args.core) if args.core else catalog(args.toml, args.rvx)
        for record in records:
            print(json.dumps(record, sort_keys=True))
    except (OSError, ValueError) as error:
        sys.exit(f"assets: {error}")


if __name__ == "__main__":
    sys.exit(main())
