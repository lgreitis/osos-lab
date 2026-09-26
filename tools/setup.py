#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare pinned upstream checkouts and apply the project patches."""

import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def git(directory, *args):
    return subprocess.check_output(["git", "-C", str(directory), *args], text=True)


def verify(name, spec):
    directory = ROOT / "vendor" / name
    if not (directory / ".git").exists():
        raise ValueError(f"{name}: checkout missing; run tools/setup.py")
    if git(directory, "rev-parse", "HEAD").strip() != spec["commit"]:
        raise ValueError(f"{name}: checkout revision differs from dependencies.lock")
    return directory


def prepare(name, spec, source=None):
    directory = ROOT / "vendor" / name
    if directory.exists():
        verify(name, spec)
    else:
        directory.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                "git",
                "clone",
                "--no-hardlinks",
                "--no-checkout",
                str(source or spec["url"]),
                str(directory),
            ],
            check=True,
        )
        git(directory, "remote", "set-url", "origin", spec["url"])
        git(directory, "checkout", "--detach", spec["commit"])
    for patch in spec["patches"]:
        path = str(ROOT / patch)
        applied = subprocess.run(
            ["git", "-C", str(directory), "apply", "--reverse", "--check", path],
            capture_output=True,
        )
        if applied.returncode:
            git(directory, "apply", "--check", path)
            git(directory, "apply", path)
    print(f"{name}: ready")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--rockbox-source",
        type=Path,
        help="Optional local clone to seed an offline checkout",
    )
    parser.add_argument(
        "--check", action="store_true", help="Only verify prepared dependencies"
    )
    args = parser.parse_args()
    dependencies = json.loads((ROOT / "dependencies.lock").read_text())
    for name, spec in dependencies.items():
        if args.check:
            verify(name, spec)
            print(f"{name}: verified")
        else:
            prepare(name, spec, args.rockbox_source)


if __name__ == "__main__":
    main()
