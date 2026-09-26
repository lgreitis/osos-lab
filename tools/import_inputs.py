#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Import already-decrypted baseline inputs, checking every hash before copying."""

import argparse
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--osos", required=True, type=Path)
    parser.add_argument("--apple-loader", required=True, type=Path)
    parser.add_argument("--nor", required=True, type=Path)
    parser.add_argument(
        "--modules",
        required=True,
        type=Path,
        help="Extracted PE modules, named Name.pe32 or Name-GUID.pe32",
    )
    parser.add_argument("--out", type=Path, default=ROOT / "inputs")
    args = parser.parse_args()
    target = json.loads((ROOT / "targets/classic7g-2.0.4.json").read_text())
    sources = {
        "osos.bin": args.osos,
        "apple-loader.bin": args.apple_loader,
        "nor.bin": args.nor,
    }
    for name in target["inputs"]:
        if name.startswith("modules/"):
            stem = Path(name).stem
            candidates = list(args.modules.glob(stem + "-*.pe32")) + list(
                args.modules.glob(stem + ".pe32")
            )
            if len(candidates) != 1:
                raise ValueError(f"Expected exactly one {stem} module")
            sources[name] = candidates[0]
    # Complete validation before writing any destination.
    for name, source in sources.items():
        data = source.read_bytes()
        expected = target["inputs"][name]
        if (
            len(data) != expected["bytes"]
            or hashlib.sha256(data).hexdigest() != expected["sha256"]
        ):
            raise ValueError(f"Input mismatch: {name}")
        destination = args.out / name
        if destination.exists() and destination.read_bytes() != data:
            raise ValueError(f"Refusing to overwrite differing input: {destination}")
    for name, source in sources.items():
        destination = args.out / name
        if not destination.exists():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
    print(f"Verified {len(sources)} baseline inputs in {args.out}")


if __name__ == "__main__":
    main()
