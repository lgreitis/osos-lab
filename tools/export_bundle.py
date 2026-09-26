#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package compiled recipes, the bootloader, and the USB helper."""

import argparse
import json
import struct
import sys
import tempfile
from pathlib import Path

import bundle

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from patching.recipe import INTERFACE, fingerprint

ROOT = Path(__file__).resolve().parents[1]


def nor_descriptor(build_dir, image):
    ipod = bundle.read(build_dir / "bootloader-ipod6g.ipod", 0x20000)
    code = ipod[8:]
    padded = (len(code) + 15) & ~15
    offset = len(image) - padded
    header = offset - 0x800
    if (
        ipod[4:8] != b"ip6g"
        or int.from_bytes(ipod[:4], "big") != (71 + sum(code)) & 0xFFFFFFFF
        or not code
        or header < 0x310
        or image[:8] != b"87021.0\x03"
        or image[header : header + 8] != b"87021.0\x02"
        or struct.unpack_from("<I", image, header + 12)[0] != padded
        or image[header + 0x40 : header + 0x50] != bytes(16)
        or image[offset:] != code.ljust(padded, b"\0")
    ):
        raise ValueError("Expected a matching Rockbox dual-boot installer")
    return {
        "schema": 1,
        "interface": INTERFACE,
        **fingerprint(image),
        "bootloader_offset": offset,
    }


def export(build_dir, helper, version, minimum, out):
    nor = bundle.read(build_dir / "install-rockbox-cfw.dfu", 0x20000)
    nor_spec = nor_descriptor(build_dir, nor)
    spec = bundle.helper_spec(helper, version, minimum)
    spec["purpose"] = "release"
    with tempfile.TemporaryDirectory(prefix="reprise-recipes-") as temporary:
        directory = Path(temporary)
        for name in ("osos", "companion"):
            spec["components"][name] = {
                "format": bundle.FORMATS[name][0],
                "files": {
                    "recipe": str((build_dir / (name + ".json")).resolve()),
                    "data": str((build_dir / (name + ".data")).resolve()),
                },
            }
        descriptor = directory / "nor.json"
        descriptor.write_text(json.dumps(nor_spec) + "\n")
        spec["components"]["nor"] = {
            "format": bundle.FORMATS["nor"][0],
            "files": {
                "image": str((build_dir / "install-rockbox-cfw.dfu").resolve()),
                "descriptor": str(descriptor),
            },
        }
        return bundle.export(spec, directory, out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--minimum-installer-version", default="0.1.0")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    export(
        args.build,
        args.helper,
        args.version,
        args.minimum_installer_version,
        args.out,
    )
    print(f"Exported firmware bundle: {args.out}")


if __name__ == "__main__":
    main()
