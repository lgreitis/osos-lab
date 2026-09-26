#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the Classic 7G CFW and its matching bootloader."""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import setup
from version import identity

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from patching.companion import build_recipe as build_companion_recipe
from patching.osos_build import build_recipe
from patching.toolchain import run

ROOT = Path(__file__).resolve().parents[1]

TARGET = ROOT / "targets/classic7g-2.0.4.json"
WORK = ROOT / ".build"


def assemble(files, out, inputs, jobs, companion=False):
    with tempfile.TemporaryDirectory(prefix="reprise-assemble-", dir=WORK) as temporary:
        image = Path(temporary) / out.name
        command = [
            "cargo",
            "run",
            "--quiet",
            "--locked",
            "--manifest-path",
            str(ROOT / "host/Cargo.toml"),
            "-j",
            str(jobs),
            "-p",
            "reprise-cli",
            "--",
            "bundle",
            "apply-recipe",
            "--recipe",
            files["recipe"],
            "--data",
            files["data"],
            "--inputs",
            str(inputs.resolve()),
            "--out",
            str(image),
        ]
        if companion:
            command += ["--nor", str((inputs / "nor.bin").resolve())]
        subprocess.run(command, check=True)
        image.replace(out)


def build_osos(out, inputs, prefix, jobs, recipe_only=False, info=None):
    print("Building OSOS recipe...", flush=True)
    info = info or identity(ROOT)
    recipe = build_recipe(
        ROOT / "payload",
        WORK / "payload",
        TARGET,
        prefix,
        jobs,
        info["revision"],
        info["version"],
    )
    files = recipe.save(out, "osos")
    if not recipe_only:
        assemble(files, out / "osos-cfw.bin", inputs, jobs)


def build_apple(out, inputs, prefix, jobs, recipe_only=False):
    print("Building Apple companion recipe...", flush=True)
    recipe = build_companion_recipe(
        ROOT / "loader/apple", WORK / "companion", TARGET, prefix, jobs
    )
    files = recipe.save(out, "companion")
    if not recipe_only:
        assemble(files, out / "cfw-loader.bin", inputs, jobs, companion=True)


def build_rockbox(out, rockbox, target, prefix, jobs):
    print("Building Rockbox bootloader...", flush=True)
    if not (rockbox / "bootloader/cfw-file-ipod6g.c").is_file():
        raise ValueError("Rockbox patch missing; run tools/setup.py")
    directory = WORK / "rockbox"
    directory.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = str(Path(prefix).parent) + os.pathsep + env.get("PATH", "")
    env["VERSION"] = target["rockbox_version"]
    makefile = directory / "Makefile"
    if not makefile.exists():
        (directory / "configure.log").write_text(
            run(
                [rockbox / "tools/configure", "--target=ipod6g", "--type=b"],
                directory,
                env,
            )
        )
        text = makefile.read_text()
        line = next(
            line
            for line in text.splitlines()
            if line.startswith("export EXTRA_DEFINES=")
        )
        makefile.write_text(text.replace(line, line + " -DIPOD_CFW_FILE_BOOT", 1))
    (directory / "build.log").write_text(run(["make", f"-j{jobs}"], directory, env))
    shutil.copyfile(
        directory / "bootloader-ipod6g.ipod", out / "bootloader-ipod6g.ipod"
    )

    packager = WORK / "packager"
    packager.mkdir(parents=True, exist_ok=True)
    sources = rockbox / "utils/mks5lboot"
    command = [
        "cc",
        "-O2",
        "-Wall",
        "-Wextra",
        '-DVERSION="' + target["rockbox_version"] + '"',
        sources / "dualboot.c",
        sources / "mkdfu.c",
        sources / "ipoddfu.c",
        sources / "main.c",
        "-o",
        "mks5lboot",
    ]
    if os.uname().sysname == "Darwin":
        command += ["-framework", "IOKit", "-framework", "CoreFoundation"]
    else:
        command += [
            "-DUSE_LIBUSBAPI",
            *run(["pkg-config", "--cflags", "--libs", "libusb-1.0"], packager).split(),
        ]
    (packager / "build.log").write_text(run(command, packager))
    (packager / "package.log").write_text(
        run(
            [
                packager / "mks5lboot",
                "--mkdfu-inst",
                out / "bootloader-ipod6g.ipod",
                out / "install-rockbox-cfw.dfu",
            ],
            out,
        )
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", help="Require a clean release tag at HEAD")
    parser.add_argument("--inputs", type=Path, default=ROOT / "inputs")
    parser.add_argument("--out", type=Path, default=ROOT / "build")
    parser.add_argument(
        "target",
        nargs="?",
        choices=["all", "osos", "osos-recipe", "loader", "loader-recipe", "bootloader"],
        default="all",
    )
    parser.add_argument(
        "--cross-prefix", default=os.environ.get("CROSS_COMPILE", "arm-elf-eabi-")
    )
    parser.add_argument("--jobs", type=int, choices=range(1, 9), default=8)
    args = parser.parse_args()
    target = json.loads(TARGET.read_text())
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which(args.cross_prefix + "gcc")
    if not compiler:
        parser.error("ARM GCC missing; supply --cross-prefix /path/to/arm-elf-eabi-")
    prefix = str(Path(compiler).absolute())[:-3]
    if (
        run([prefix + "gcc", "-dumpfullversion"], ROOT).strip()
        != target["toolchain"]["gcc"]
    ):
        parser.error("This target requires ARM GCC " + target["toolchain"]["gcc"])
    if args.target in ("all", "osos", "osos-recipe"):
        build_osos(
            out,
            args.inputs,
            prefix,
            args.jobs,
            args.target == "osos-recipe",
            identity(ROOT, args.tag),
        )
    if args.target in ("all", "loader", "loader-recipe"):
        build_apple(out, args.inputs, prefix, args.jobs, args.target == "loader-recipe")
    if args.target in ("all", "bootloader"):
        dependencies = json.loads((ROOT / "dependencies.lock").read_text())
        rockbox = setup.verify("rockbox", dependencies["rockbox"])
        build_rockbox(out, rockbox, target, prefix, args.jobs)
    print(f"Built {args.target}: {out}")


if __name__ == "__main__":
    main()
