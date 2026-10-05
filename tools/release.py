#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build target-specific release bundles without Apple firmware inputs."""

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
import target_profiles
from version import identity, valid_version

import build

ROOT = build.ROOT
RELEASE_TARGETS = ("classic6g-reva-2.0.1", "classic7g-2.0.4")


def run(*args):
    subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True)


def publish(output, destination):
    """Replace only artifacts produced by this release, preserving other targets."""
    if destination.is_symlink():
        raise ValueError("Output directory must not be a symlink")
    destination.mkdir(parents=True, exist_ok=True)
    artifacts = list(output.iterdir())
    for artifact in artifacts:
        previous = destination / artifact.name
        if previous.is_symlink():
            raise ValueError(f"Output artifact must not be a symlink: {previous}")
    with tempfile.TemporaryDirectory(
        prefix=".publish-", dir=destination.parent
    ) as temporary:
        staging = Path(temporary)
        incoming, backup = staging / "incoming", staging / "backup"
        shutil.copytree(output, incoming)
        backup.mkdir()
        installed = []
        try:
            for artifact in artifacts:
                previous = destination / artifact.name
                if previous.exists():
                    previous.rename(backup / artifact.name)
                (incoming / artifact.name).rename(previous)
                installed.append(previous)
        except BaseException:
            for path in installed:
                if path.is_dir():
                    shutil.rmtree(path)
                else:
                    path.unlink()
            for path in backup.iterdir():
                path.rename(destination / path.name)
            raise


def build_target(config, output, rockbox, info, minimum):
    output.mkdir()
    build.build_osos(config, output, None, recipe_only=True, info=info)
    build.build_apple(config, output, None, recipe_only=True)
    build.build_rockbox(config, output, rockbox)
    helper = config.work / "helper"
    run(
        sys.executable,
        ROOT / "usb-helper/build.py",
        "--rockbox",
        rockbox,
        "--toolchain",
        Path(config.prefix).parent,
        "--out",
        helper,
        "--jobs",
        config.jobs,
    )
    (output / "helper").mkdir()
    for name in ("upload.dfu", "manifest.json"):
        shutil.copyfile(helper / name, output / "helper" / name)
    export_bundle.export(
        output,
        output / "helper",
        info["version"],
        minimum,
        output / "package",
        config.target["target"],
    )
    bundle.pack(output / "package", output / "repriseos.zip")


def archive_sources(output, tag, rockbox, dependencies):
    for repo, revision, name in (
        (ROOT, tag, "osos-lab"),
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", help="Require a clean vSemVer tag at HEAD")
    parser.add_argument(
        "--cross-prefix", default=os.environ.get("CROSS_COMPILE", "arm-elf-eabi-")
    )
    parser.add_argument("--jobs", type=int, choices=range(1, 9), default=8)
    parser.add_argument(
        "--firmware-target",
        choices=RELEASE_TARGETS,
        help="Build only this target; the default builds all release targets",
    )
    parser.add_argument("--minimum-installer-version", default="0.1.0")
    parser.add_argument("--out", type=Path, default=ROOT / "build/releases")
    args = parser.parse_args()
    if not valid_version(args.minimum_installer_version):
        parser.error("Minimum installer version must be SemVer")
    info = identity(ROOT, args.tag)
    targets = [
        target_profiles.load(name)
        for name in (
            (args.firmware_target,) if args.firmware_target else RELEASE_TARGETS
        )
    ]
    prefixes = [
        build.compiler_prefix(args.cross_prefix, t["toolchain"]["gcc"]) for t in targets
    ]
    dependencies = json.loads((ROOT / "dependencies.lock").read_text())
    rockbox = setup.verify("rockbox", dependencies["rockbox"])
    work = ROOT / ".build"
    work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="release-", dir=work) as temporary:
        staging = Path(temporary)
        output = staging / "output"
        output.mkdir()
        for target, prefix in zip(targets, prefixes, strict=True):
            name = target["target"]
            config = build.BuildConfig(
                target_profiles.DIRECTORY / f"{name}.json",
                staging / name,
                prefix,
                args.jobs,
            )
            build_target(
                config, output / name, rockbox, info, args.minimum_installer_version
            )
        if args.tag:
            archive_sources(output, args.tag, rockbox, dependencies)
        destination = args.out.absolute()
        publish(output, destination)
    for target in targets:
        print(
            f"Built {info['version']}: {destination / target['target'] / 'repriseos.zip'}"
        )


if __name__ == "__main__":
    main()
