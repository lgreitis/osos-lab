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

import target_profiles

from patching.compatibility import payload_shim
from patching.ui import Resources, generate
from patching.ui.resources import Resource, Template

ROOT = Path(__file__).resolve().parents[1]


class Templates(Mapping):
    def __init__(self, firmware, bank, expected_header):
        self.firmware = firmware
        self.index = {}
        self.used = {}
        version, data_offset, count = struct.unpack_from("<III", firmware, bank)
        if (version, data_offset, count) != tuple(expected_header):
            raise ValueError("Resource bank does not match the selected OSOS shim")
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


def extract(firmware, sources, shim):
    resources = Resources([], shim["bindings"])
    templates = Templates(firmware, shim["bank_offset"], shim["bank_header"])
    resources.original = templates
    with tempfile.TemporaryDirectory(prefix="reprise-ui-metadata-") as directory:
        generate(resources, sources, Path(directory), "metadata")
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
    parser.add_argument("--firmware-target", default=target_profiles.DEFAULT_TARGET)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    firmware = args.input.read_bytes()
    profile = target_profiles.load(args.firmware_target)
    target_path = target_profiles.DIRECTORY / f"{profile['target']}.json"
    fingerprint = profile["inputs"]["osos.bin"]
    shim = json.loads(
        (payload_shim(ROOT / "payload", target_path) / "ui.json").read_text()
    )
    if fingerprint != {
        "bytes": len(firmware),
        "sha256": hashlib.sha256(firmware).hexdigest(),
    }:
        raise ValueError("Expected the pinned original OSOS image")
    metadata = {
        "schema": 1,
        "input": fingerprint,
        "resources": extract(firmware, sorted((ROOT / "payload").glob("*.ui")), shim),
    }
    out = args.out or target_path.with_name(target_path.stem + "-ui.json")
    out.write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    main()
