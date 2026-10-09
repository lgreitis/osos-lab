#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare verified 2.0.5 inputs from preserved IPSW, OSOS and AUPD plaintext."""

import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ipsw", required=True, type=Path)
    parser.add_argument("--osos", required=True, type=Path)
    parser.add_argument("--aupd", required=True, type=Path, help="Decrypted AUPD body")
    parser.add_argument(
        "--reprise", type=Path, default=ROOT / "host/target/release/reprise"
    )
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    subprocess.run(
        [
            str(args.reprise.resolve()),
            "firmware",
            "prepare",
            "--ipsw",
            str(args.ipsw),
            "--osos",
            str(args.osos),
            "--aupd",
            str(args.aupd),
            "--out",
            str(args.out),
        ],
        check=True,
    )


if __name__ == "__main__":
    main()
