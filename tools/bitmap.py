#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Convert an edited PNG to an Apple BMap without changing its pixels."""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from patching.bitmap import encode_bmap, read_png


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    width, height, pixels = read_png(args.input)
    bitmap = encode_bmap(width, height, pixels)
    args.output.write_bytes(bitmap)
    print(f"{width}x{height}: {len(bitmap):,} bytes -> {args.output}")


if __name__ == "__main__":
    main()
