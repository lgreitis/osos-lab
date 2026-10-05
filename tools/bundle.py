#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Export release assets and package local ZIPs."""

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

import target_profiles

ROOT = Path(__file__).resolve().parents[1]


def read(path, limit):
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"Expected a regular file: {path}")
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if not data or len(data) > limit:
        raise ValueError(f"Empty or oversized input: {path}")
    return data


def invoke(*arguments):
    result = subprocess.run(
        [
            "cargo",
            "run",
            "--quiet",
            "--locked",
            "--manifest-path",
            str(ROOT / "host/Cargo.toml"),
            "-j",
            "8",
            "-p",
            "reprise-cli",
            "--",
            "bundle",
            *map(str, arguments),
            "--json",
        ],
        capture_output=True,
        text=True,
    )
    if result.returncode:
        message = result.stderr.strip() or result.stdout.strip()
        if result.stdout:
            try:
                message = json.loads(result.stdout)["error"]
            except (ValueError, KeyError, TypeError):
                pass
        raise ValueError(message or f"Bundle command exited with {result.returncode}")
    return json.loads(result.stdout)


def export(spec, base, output):
    with tempfile.TemporaryDirectory(prefix="bundle-spec-") as temporary:
        path = Path(temporary) / "spec.json"
        path.write_text(json.dumps(spec))
        return invoke(
            "export",
            "--spec",
            path,
            "--base",
            base.absolute(),
            "--out",
            output.absolute(),
        )


def helper_spec(directory, version, minimum, target=target_profiles.DEFAULT_TARGET):
    return {
        "purpose": "development",
        "version": version,
        "minimum_installer_version": minimum,
        "compatibility": target_profiles.load(target)["compatibility"],
        "components": {
            "usb_helper": {
                "format": "reprise-upload-v3",
                "files": {
                    "image": str(directory.resolve() / "upload.dfu"),
                    "descriptor": str(directory.resolve() / "manifest.json"),
                },
            }
        },
    }


def pack(directory, output):
    return invoke(
        "pack", "--directory", directory.absolute(), "--out", output.absolute()
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument(
        "--spec",
        type=Path,
        help="Component files are relative to this JSON specification",
    )
    source.add_argument(
        "--helper",
        type=Path,
        help="Export only a built helper as a development bundle",
    )
    source.add_argument(
        "--pack", type=Path, help="Package an exported directory as a ZIP"
    )
    parser.add_argument(
        "--target",
        default=target_profiles.DEFAULT_TARGET,
        help="Target profile for --helper",
    )
    parser.add_argument("--version", help="Required with --helper")
    parser.add_argument("--minimum-installer-version", default="0.1.0")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        if not args.helper and args.target != target_profiles.DEFAULT_TARGET:
            parser.error(
                "--target requires --helper; other sources carry their own compatibility"
            )
        if args.helper:
            if not args.version:
                parser.error("--helper requires --version")
            spec = helper_spec(
                args.helper, args.version, args.minimum_installer_version, args.target
            )
            base = Path.cwd()
        elif args.spec:
            if args.version or args.minimum_installer_version != "0.1.0":
                parser.error("Version options belong in the specification with --spec")
            spec = json.loads(read(args.spec, 256 * 1024))
            base = args.spec.resolve().parent
        else:
            if args.version or args.minimum_installer_version != "0.1.0":
                parser.error(
                    "Version options belong in the exported manifest with --pack"
                )
        manifest = (
            pack(args.pack, args.out.absolute())
            if args.pack
            else export(spec, base, args.out.absolute())
        )
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(1, f"Error: {error}\n")
    print(f"Exported {manifest['purpose']} bundle {manifest['version']}: {args.out}")


if __name__ == "__main__":
    main()
