# SPDX-License-Identifier: GPL-3.0-only
"""Shared disk-theme format and firmware catalog fingerprint."""

import hashlib
import json
import struct

HEADER = struct.Struct("<4s32sIII")
METADATA = struct.Struct("<I48s")
RECORD = struct.Struct("<III")
MAGIC = b"RTH2"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def catalog_hash(catalog):
    return bytes.fromhex(digest(json.dumps(catalog, sort_keys=True).encode()))
