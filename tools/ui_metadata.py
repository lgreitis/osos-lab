#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refresh UI structure metadata from the pinned local OSOS image."""

import argparse
import hashlib
import json
import struct
import sys
import tempfile
from collections.abc import Mapping
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from patching import native_ui
from patching.resource import Resource, Template

ROOT = Path(__file__).resolve().parents[1]


class Templates(Mapping):
    def __init__(self, firmware):
        self.firmware = firmware
        self.index = {}
        self.used = {}
        bank = native_ui.BANK_OFFSET
        version, data_offset, count = struct.unpack_from("<III", firmware, bank)
        if (version, data_offset, count) != (3, 0x176D0, 27):
            raise ValueError("Unexpected FW2.0.4 resource bank")
        for i in range(count):
            kind, entries, _, table = struct.unpack_from(
                "<IIII", firmware, bank + 12 + i * 16
            )
            kind = kind.to_bytes(4, "big").decode("ascii")
            for j in range(entries):
                resource_id, offset, size = struct.unpack_from(
                    "<III", firmware, bank + table + j * 12
                )
                start = bank + data_offset + offset
                if (
                    start + size > len(firmware)
                    or 0x0CF00000 <= resource_id < 0x0D000000
                ):
                    raise ValueError(
                        "Invalid native resource range or reserved ID collision"
                    )
                self.index[kind, resource_id] = (start, size)

    def __getitem__(self, key):
        if key not in self.used:
            offset, size = self.index[key]
            self.used[key] = Template(offset, size, {}, self.firmware)
        return Resource.template(self.used[key])

    def __iter__(self):
        return iter(self.index)

    def __len__(self):
        return len(self.index)

    def __contains__(self, key):
        return key in self.index


def extract(firmware, sources, prefix):
    resources = native_ui.Resources([])
    templates = Templates(firmware)
    resources.original = templates
    with tempfile.TemporaryDirectory(prefix="reprise-ui-metadata-") as directory:
        native_ui.generate(resources, sources, Path(directory), prefix, "metadata")
    return [
        {
            "kind": kind,
            "id": resource_id,
            "offset": template.offset,
            "bytes": template.size,
            "words": dict(sorted(template.words.items())),
        }
        for (kind, resource_id), template in sorted(templates.used.items())
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=ROOT / "inputs/osos.bin")
    parser.add_argument(
        "--out", type=Path, default=ROOT / "targets/classic7g-2.0.4-ui.json"
    )
    parser.add_argument("--cross-prefix", default="arm-elf-eabi-")
    args = parser.parse_args()
    firmware = args.input.read_bytes()
    fingerprint = json.loads((ROOT / "targets/classic7g-2.0.4.json").read_text())[
        "inputs"
    ]["osos.bin"]
    if fingerprint != {
        "bytes": len(firmware),
        "sha256": hashlib.sha256(firmware).hexdigest(),
    }:
        raise ValueError("Expected the pinned original OSOS image")
    metadata = {
        "schema": 1,
        "input": fingerprint,
        "resources": extract(
            firmware, sorted((ROOT / "payload").glob("*.ui")), args.cross_prefix
        ),
    }
    args.out.write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    main()
