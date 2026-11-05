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


def digest(data):
    return hashlib.sha256(data).hexdigest()


def catalog_bytes(catalog):
    return json.dumps(catalog, sort_keys=True, separators=(",", ":")).encode()


def catalog_hash(catalog):
    return bytes.fromhex(digest(catalog_bytes(catalog)))
