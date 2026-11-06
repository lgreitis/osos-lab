# SPDX-License-Identifier: GPL-3.0-only
"""Shared disk-theme format and firmware catalog fingerprint."""

import hashlib
import json
import struct

# RTH4 appearance pack: magic, catalog hash, appearance (0 light, 1 dark), body bytes,
# record count and body CRC32. The body is the style prefix followed by records.
HEADER = struct.Struct("<4s32sIIII")
RECORD = struct.Struct("<III")
MAGIC = b"RTH4"
# Upper bound on catalog assets, mirrored by CFW_THEME_MAX_ASSETS in the payload.
# The style prefix carries one tint role byte per asset (TINT_BYTES bytes).
MAX_ASSETS = 640
TINT_BYTES = MAX_ASSETS


def digest(data):
    return hashlib.sha256(data).hexdigest()


def catalog_bytes(catalog):
    return json.dumps(catalog, sort_keys=True, separators=(",", ":")).encode()


def catalog_hash(catalog):
    return bytes.fromhex(digest(catalog_bytes(catalog)))
