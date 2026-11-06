# SPDX-License-Identifier: GPL-3.0-only
"""Extract a full rollback baseline from the user-provided, pinned stock OSOS.

This is installation-time data, not an extra resident copy of stock artwork.
Never distribute the extracted Apple graphics with a firmware release.
"""

import argparse
import json
import zlib
from pathlib import Path

from patching.catalog import is_payload, payload_bitmaps
from patching.theme_format import HEADER, MAGIC, RECORD, catalog_hash, digest
from themes.style import classic_style

ROOT = Path(__file__).resolve().parents[1]


def compile_baseline(catalog, firmware, sources=None):
    """The Classic rollback pack: every catalog bitmap's original bytes. Firmware
    bitmaps come from the pinned image, payload catalog bitmaps from the UI
    sources in `sources` (default: this repository's payload folder)."""
    if catalog.get("schema") != 1 or catalog.get("input") != {
        "bytes": len(firmware),
        "sha256": digest(firmware),
    }:
        raise ValueError("Expected the pinned original OSOS image")
    body = bytearray(classic_style("light"))
    assets = catalog["bitmaps"]
    if not assets or len(assets) > 1024:
        raise ValueError("Unsupported bitmap catalog size")
    block = None
    for index, asset in enumerate(assets):
        start, size, offset = asset["offset"], asset["bytes"], asset["data_offset"]
        if is_payload(asset):
            if block is None:
                block = {
                    name: data
                    for name, _, data in payload_bitmaps(sources or ROOT / "payload")
                }
            original = block.get(asset.get("name"), b"")
            if len(original) != size or not 0 < offset < size:
                raise ValueError("Payload bitmap does not match the catalog")
        else:
            if (
                not 0 <= start <= len(firmware)
                or not 0 < offset < size <= len(firmware) - start
            ):
                raise ValueError("Bitmap range outside firmware")
            original = firmware[start : start + size]
        if zlib.crc32(original) != asset["crc32"]:
            raise ValueError("Stock bitmap checksum mismatch")
        payload = original[offset:]
        body += RECORD.pack(index, offset, len(payload))
        body += payload
        body += bytes(-len(payload) % 4)
    return (
        HEADER.pack(
            MAGIC, catalog_hash(catalog), 0, len(body), len(assets), zlib.crc32(body)
        )
        + body
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, default=Path("firmware/bitmaps.json"))
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        pack = compile_baseline(
            json.loads(args.catalog.read_text()), args.input.read_bytes()
        )
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_bytes(pack)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"{error}\n")
    print(f"Stock rollback baseline: {len(pack):,} bytes, {args.out}")


if __name__ == "__main__":
    main()
