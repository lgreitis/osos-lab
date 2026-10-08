#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sign the Classic bundle and collect GitHub release assets using a built CLI."""

import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from release import ROOT


def invoke(reprise, *arguments):
    result = subprocess.run(
        [str(reprise), "bundle", *map(str, arguments), "--json"],
        capture_output=True,
        text=True,
    )
    if result.returncode:
        raise ValueError(result.stdout.strip() or result.stderr.strip())
    return json.loads(result.stdout)


def collect(directory, output, reprise, seed, key, version):
    if output.exists() or output.is_symlink():
        raise ValueError(f"Release asset directory already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=".release-assets-", dir=output.parent
    ) as tmp:
        staging = Path(tmp)
        assets = staging / "assets"
        assets.mkdir()
        target = "classic"
        package = staging / target
        shutil.copytree(directory / target / "package", package)
        invoke(reprise, "sign", "--directory", package, "--seed", seed)
        report = invoke(reprise, "inspect", "--directory", package, "--key", key)
        manifest = report["manifest"]
        if (
            manifest["compatibility"]["target"] != target
            or manifest["version"] != version
            or manifest["purpose"] != "release"
        ):
            raise ValueError(f"Release identity mismatch: {target}")
        invoke(
            reprise,
            "pack",
            "--directory",
            package,
            "--out",
            assets / f"repriseos-{target}.zip",
        )
        for name in ("manifest.json", "manifest.json.sig"):
            shutil.copyfile(package / name, assets / f"{target}-{name}")
        for digest in manifest["assets"]:
            name = f"{digest}.blob"
            if not (assets / name).exists():
                shutil.copyfile(package / name, assets / name)
        for name in ("osos-lab-source.tar.gz", "rockbox-source.tar.gz"):
            shutil.copyfile(directory / name, assets / name)
        shutil.copyfile(ROOT / "COPYING", assets / "COPYING")
        checksums = [
            f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
            for path in sorted(assets.iterdir())
        ]
        (assets / "SHA256SUMS").write_text("".join(checksums))
        assets.rename(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "build/releases")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--reprise", type=Path, required=True)
    parser.add_argument("--seed", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    collect(
        args.directory,
        args.out,
        args.reprise.resolve(),
        args.seed,
        args.key,
        args.version,
    )


if __name__ == "__main__":
    main()
