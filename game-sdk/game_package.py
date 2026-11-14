# SPDX-License-Identifier: GPL-3.0-only
"""Native game executable validation and reproducible ZIP entries."""

import plistlib
import struct
import zipfile

LOAD_ADDRESS = 0x18000000
MAX_IMAGE_BYTES = 0x100000


def validate_image(image):
    if not 0x28 <= len(image) <= MAX_IMAGE_BYTES:
        raise ValueError("Executable exceeds native image bounds")

    magic, version, count, unknown, imports, *entries = struct.unpack_from(
        "<10I", image
    )
    if (magic, version, count, unknown, imports) != (0x70706165, 0x10001000, 5, 0, 0):
        raise ValueError("Unexpected eapp header")
    for entry in entries:
        if entry % 4 or not LOAD_ADDRESS + 0x28 <= entry < LOAD_ADDRESS + len(image):
            raise ValueError("Callback is outside the ARM executable")


def write_entry(bundle, name, data):
    entry = zipfile.ZipInfo(str(name), date_time=(2026, 1, 1, 0, 0, 0))
    entry.compress_type = zipfile.ZIP_DEFLATED
    entry.external_attr = 0o100644 << 16
    bundle.writestr(entry, data)


def manifest(name, package_id, executable, artwork, contents, heap_kib):
    return plistlib.dumps(
        {
            "Name": name,
            "BuildIdentifier": package_id,
            "GUID": package_id,
            "Version": "0.2.0",
            "HeapSize": heap_kib,
            "UserDataPath": package_id,
            "Files": [
                {"Path": path, "Size": len(data), "DRM": False, "Verify": False}
                for path, data in contents.items()
            ],
            "Platforms": [
                {
                    "PlatformID": 3,
                    "PlatformVersion": 1,
                    "BuildID": 1,
                    "ExecutablePath": executable,
                    "LaunchingArtwork": artwork,
                }
            ],
        },
        fmt=plistlib.FMT_XML,
        sort_keys=False,
    )
