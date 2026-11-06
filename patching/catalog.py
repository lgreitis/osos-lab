# SPDX-License-Identifier: GPL-3.0-only
"""Payload bitmaps that extend the firmware bitmap catalog.

The theme engine addresses bitmaps by catalog index. The firmware's own BMap
bank cannot grow, so RepriseOS preview bitmaps (``<bitmap format="gray4">`` in
the UI sources) live in one contiguous block inside the payload and are listed
after the firmware bitmaps with ``"source": "payload"`` and offsets relative to
that block. Their order is the document order of UI_SOURCES, and their resource
ids come from a fixed range so the catalog, the generated payload and the host
tools agree without sharing allocator state."""

from .bitmap import encode_bmap, read_png

# UI sources in generation order; the payload bitmap block follows it.
UI_SOURCES = (
    "cfw_info.ui",
    "custom_eq.ui",
    "play_next.ui",
    "song_info.ui",
    "album_artists.ui",
    "theme_settings.ui",
    "accent_settings.ui",
    "dark_mode.ui",
    "menu_covers.ui",
    "queue_selection.ui",
)
# Catalog bitmaps take ids from the top of the reserved CFW range (0x0CF00000 up),
# far above the sequential allocations of the UI generator.
CATALOG_ID_BASE = 0x0CFF0000
CATALOG_FORMAT = "gray4"


def align(size):
    return (size + 3) & ~3


def catalog_bitmap_id(position):
    return CATALOG_ID_BASE + position


def payload_bitmaps(source):
    """(name, resource id, native bytes) of every catalog bitmap, in block order."""
    from .ui.parser import read  # the ui package imports this module

    bitmaps = []
    for name in UI_SOURCES:
        path = source / name
        document = read(path)
        for bitmap_name, (filename, bitmap_format) in document.bitmaps.items():
            if bitmap_format != CATALOG_FORMAT:
                continue
            data = encode_bmap(*read_png(path.parent / filename), gray4=True)
            bitmaps.append((bitmap_name, catalog_bitmap_id(len(bitmaps)), data))
    return bitmaps


def block_offsets(bitmaps):
    """Offset of each bitmap inside the block, each starting word-aligned."""
    offsets, at = [], 0
    for _, _, data in bitmaps:
        offsets.append(at)
        at += align(len(data))
    return offsets


def payload_entries(source, describe):
    """Catalog entries for the payload block, described like firmware bitmaps."""
    bitmaps = payload_bitmaps(source)
    entries = []
    for (name, resource_id, data), offset in zip(bitmaps, block_offsets(bitmaps)):
        entry = describe(resource_id, offset, data, name)
        entry["source"] = "payload"
        entries.append(entry)
    return entries


def is_payload(asset):
    return asset.get("source") == "payload"
