#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build a complete recipe bundle without Apple firmware inputs."""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import bundle
import export_bundle
import setup
from version import identity

import build

ROOT = build.ROOT


def run(*args):
    subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", help="Require a clean vSemVer tag at HEAD")
    parser.add_argument(
        "--cross-prefix", default=os.environ.get("CROSS_COMPILE", "arm-elf-eabi-")
    )
    parser.add_argument("--jobs", type=int, choices=range(1, 9), default=8)
    args = parser.parse_args()
    info = identity(ROOT, args.tag)
    compiler = shutil.which(args.cross_prefix + "gcc")
    if not compiler:
        parser.error("ARM GCC missing; supply --cross-prefix")
    prefix = str(Path(compiler).absolute())[:-3]
    target = json.loads(build.TARGET.read_text())
    if (
        build.run([prefix + "gcc", "-dumpfullversion"], ROOT).strip()
        != target["toolchain"]["gcc"]
    ):
        parser.error("This target requires ARM GCC " + target["toolchain"]["gcc"])
    dependencies = json.loads((ROOT / "dependencies.lock").read_text())
    rockbox = setup.verify("rockbox", dependencies["rockbox"])
    build.WORK.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="release-", dir=build.WORK) as temporary:
        staging = Path(temporary)
        output = staging / "output"
        output.mkdir()
        build.WORK = staging / "work"
        build.build_osos(output, None, prefix, args.jobs, recipe_only=True, info=info)
        build.build_apple(output, None, prefix, args.jobs, recipe_only=True)
        build.build_rockbox(output, rockbox, target, prefix, args.jobs)
        helper = staging / "helper"
        run(
            sys.executable,
            ROOT / "usb-helper/build.py",
            "--rockbox",
            rockbox,
            "--toolchain",
            Path(prefix).parent,
            "--out",
            helper,
            "--jobs",
            args.jobs,
        )
        (output / "helper").mkdir()
        for name in ("upload.dfu", "manifest.json"):
            shutil.copyfile(helper / name, output / "helper" / name)
        export_bundle.export(
            output, output / "helper", info["version"], "0.1.0", output / "package"
        )
        bundle.pack(output / "package", output / "repriseos.zip")
        if args.tag:
            for repo, revision, name in (
                (ROOT, args.tag, "osos-lab"),
                (rockbox, dependencies["rockbox"]["commit"], "rockbox"),
            ):
                run(
                    "git",
                    "-C",
                    repo,
                    "archive",
                    "--format=tar.gz",
                    f"--prefix={name}/",
                    "--output",
                    output / f"{name}-source.tar.gz",
                    revision,
                )
        destination = ROOT / "build"
        if destination.is_symlink():
            raise ValueError("build/ must not be a symlink")
        if destination.exists():
            shutil.rmtree(destination)
        output.rename(destination)
    print(f"Built {info['version']}: {destination / 'repriseos.zip'}")


if __name__ == "__main__":
    main()
