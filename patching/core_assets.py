# SPDX-License-Identifier: GPL-3.0-only
"""The runtime's own bitmaps, payload/rvx/assets/<name>.png as Settings pane
icons (menu masks), lz-compressed behind a header and an rvx entry table after
the runtime in osos-cfw.bin. Entry offsets are relative to the trailer; the
runtime reads an icon while it is shown."""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rvx-sdk"))
import bitmap
import rvx

MAGIC = 0x41435652  # RVCA
HEADER = struct.Struct("<4I")  # magic, count, bytes, reserved


def build(folder):
    pngs = sorted(folder.glob("*.png"))
    entries = bytearray()
    payloads = bytearray()
    base = HEADER.size + rvx.ENTRY.size * len(pngs)
    for png in pngs:
        mask = bitmap.png_to_bmap(png.read_bytes(), "icon")
        stream = rvx.lz_encode(mask)
        entries += rvx.ENTRY.pack(
            rvx.fnv1a(png.stem.encode()),
            rvx.KIND_BITMAP,
            rvx.LZ,
            rvx.PRELOAD.index("optional"),
            0,
            base + len(payloads),
            len(stream),
            len(mask),
        )
        payloads += rvx.padded(stream)
    total = base + len(payloads)
    return HEADER.pack(MAGIC, len(pngs), total, 0) + entries + payloads
