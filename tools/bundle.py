#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Export release assets and package local ZIPs."""

import argparse
import hashlib
import json
import re
import shutil
import tempfile
import zipfile
from pathlib import Path

MAX_ASSET = 32 * 1024 * 1024
MAX_TOTAL = 64 * 1024 * 1024
FORMATS = {
    "usb_helper": ("reprise-upload-v3", {"image", "descriptor"}),
    "osos": ("reprise-osos-recipe-v2", {"recipe", "data"}),
    "companion": ("reprise-companion-recipe-v2", {"recipe", "data"}),
    "nor": ("reprise-nor-template-v1", {"image", "descriptor"}),
}
COMPATIBILITY = {
    "target": "classic7g-2.0.4",
    "models": ["MC293", "MC297"],
    "hardware_version": 0x00130200,
    "apple_firmware": "2.0.4",
    "bootrom_sha256": "69c087afc5753d7f0f11f09b141b372af753a854bc526673ea444c486d6003e4",
}


def read(path, limit):
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"Expected a regular file: {path}")
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if not data or len(data) > limit:
        raise ValueError(f"Empty or oversized input: {path}")
    return data


def valid_version(value):
    number = r"(?:0|[1-9][0-9]*)"
    if not isinstance(value, str) or not re.fullmatch(
        rf"{number}\.{number}\.{number}(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
        r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?",
        value,
    ):
        return False
    prerelease = value.partition("+")[0].partition("-")[2]
    return all(
        not p.isdigit() or p == "0" or not p.startswith("0")
        for p in prerelease.split(".")
    )


def export(spec, base, output):
    required = {
        "purpose",
        "version",
        "minimum_installer_version",
        "compatibility",
        "components",
    }
    if (
        not isinstance(spec, dict)
        or set(spec) != required
        or spec["purpose"] not in ("development", "release")
    ):
        raise ValueError("Invalid bundle specification fields/purpose")
    if not all(
        valid_version(spec[k]) for k in ("version", "minimum_installer_version")
    ):
        raise ValueError("Versions must be SemVer")
    compatibility = dict(spec["compatibility"])
    models = compatibility.pop("models", [])
    expected = {k: v for k, v in COMPATIBILITY.items() if k != "models"}
    if (
        compatibility != expected
        or not isinstance(models, list)
        or not models
        or any(model not in ("MC293", "MC297") for model in models)
        or len(models) != len(set(models))
    ):
        raise ValueError("Unsupported bundle target")
    components = spec["components"]
    if not components or not set(components) <= FORMATS.keys():
        raise ValueError("Unknown or empty component set")
    if spec["purpose"] == "release" and set(components) != FORMATS.keys():
        raise ValueError(
            "A release requires OSOS, companion, NOR, and USB helper components"
        )
    manifest = {"schema": 1, **spec, "components": {}, "assets": {}}
    blobs = {}
    for name, component in sorted(components.items()):
        fmt, required_files = FORMATS[name]
        if set(component) != {"format", "files"} or component["format"] != fmt:
            raise ValueError(f"Invalid {name} component format")
        files = component["files"]
        if not required_files <= files.keys():
            raise ValueError(f"Missing required {name} component files")
        references = {}
        for logical, filename in sorted(files.items()):
            if not re.fullmatch(r"[a-z0-9_]+", logical):
                raise ValueError("Invalid logical file name")
            data = read(base / filename, MAX_ASSET)
            digest = hashlib.sha256(data).hexdigest()
            blobs[digest] = data
            if sum(map(len, blobs.values())) > MAX_TOTAL:
                raise ValueError("Bundle exceeds size limit")
            references[logical] = digest
            manifest["assets"][digest] = {"bytes": len(data)}
        if name == "usb_helper":
            validate_helper(blobs[references["image"]], blobs[references["descriptor"]])
        manifest["components"][name] = {"format": fmt, "files": references}
    raw = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    if len(raw) > 256 * 1024:
        raise ValueError("Manifest too large")
    if output.exists() or output.is_symlink():
        raise ValueError("Output already exists; choose a new directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".bundle-", dir=output.parent))
    try:
        (staging / "manifest.json").write_bytes(raw)
        for digest, data in blobs.items():
            (staging / (digest + ".blob")).write_bytes(data)
        # Reserve the destination exclusively before publishing files into it.
        output.mkdir()
        try:
            for path in staging.iterdir():
                path.rename(output / path.name)
        except BaseException:
            shutil.rmtree(output)
            raise
    finally:
        shutil.rmtree(staging)
    return manifest


def validate_helper(image, raw):
    descriptor = json.loads(raw)
    if (
        descriptor.get("schema") != 3
        or descriptor.get("storage_inspection") is not True
        or descriptor.get("mode") != "stream-file"
        or descriptor.get("rom_sha256") != COMPATIBILITY["bootrom_sha256"]
        or descriptor.get("bytes") != len(image)
        or descriptor.get("sha256") != hashlib.sha256(image).hexdigest()
    ):
        raise ValueError("Expected a matching v3 helper with storage inspection")


def helper_spec(directory, version, minimum):
    return {
        "purpose": "development",
        "version": version,
        "minimum_installer_version": minimum,
        "compatibility": COMPATIBILITY,
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
    raw = read(directory / "manifest.json", 256 * 1024)
    manifest = json.loads(raw)
    files = {"manifest.json": raw}
    total = 0
    for digest, asset in manifest["assets"].items():
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError("Invalid asset SHA-256")
        data = read(directory / f"{digest}.blob", MAX_ASSET)
        if len(data) != asset["bytes"] or hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f"Asset size/hash mismatch: {digest}")
        total += len(data)
        if total > MAX_TOTAL:
            raise ValueError("Bundle exceeds size limit")
        files[f"{digest}.blob"] = data
    signature = directory / "manifest.json.sig"
    if signature.exists():
        data = read(signature, 64)
        if len(data) != 64:
            raise ValueError("Invalid signature length")
        files[signature.name] = data
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("xb") as stream:
        try:
            with zipfile.ZipFile(
                stream, "w", compression=zipfile.ZIP_DEFLATED
            ) as archive:
                for name, data in sorted(files.items()):
                    entry = zipfile.ZipInfo(name)
                    entry.compress_type = zipfile.ZIP_DEFLATED
                    entry.external_attr = 0o100644 << 16
                    archive.writestr(entry, data)
        except BaseException:
            output.unlink()
            raise
    return manifest


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
    parser.add_argument("--version", help="Required with --helper")
    parser.add_argument("--minimum-installer-version", default="0.1.0")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.helper:
            if not args.version:
                parser.error("--helper requires --version")
            spec = helper_spec(
                args.helper, args.version, args.minimum_installer_version
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
