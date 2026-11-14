# SPDX-License-Identifier: GPL-3.0-only
"""Shared disk-theme format and firmware catalog fingerprint."""

import hashlib
import json
import struct

HEADER = struct.Struct("<4s32sIII")
RECORD = struct.Struct("<III")
MAGIC = b"RTH3"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def catalog_bytes(catalog):
    return json.dumps(catalog, sort_keys=True, separators=(",", ":")).encode()


def catalog_hash(catalog):
    return bytes.fromhex(digest(catalog_bytes(catalog)))
