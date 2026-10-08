#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build firmware recipes, Apple companions and the shared Classic bootloader."""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import setup
from version import identity

import firmware

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from patching.recipes.companion import build_recipe as build_companion_recipe
from patching.recipes.osos import build_recipe
from patching.toolchain import run

ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class BuildConfig:
    target_path: Path
    work: Path
    prefix: str
    jobs: int

    @property
    def target(self):
        return json.loads(self.target_path.read_text())


def compiler_prefix(value, version):
    compiler = shutil.which(value + "gcc")
    if not compiler:
        raise ValueError(
            "ARM GCC missing; supply --cross-prefix /path/to/arm-elf-eabi-"
        )
    prefix = str(Path(compiler).absolute())[:-3]
    if run([prefix + "gcc", "-dumpfullversion"], ROOT).strip() != version:
        raise ValueError("This target requires ARM GCC " + version)
    return prefix


def assemble(config, files, out, inputs, nor=None):
    config.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="reprise-assemble-", dir=config.work
    ) as temporary:
        image = Path(temporary) / out.name
        command = [
            "cargo",
            "run",
            "--quiet",
            "--locked",
            "--manifest-path",
            str(ROOT / "host/Cargo.toml"),
            "-j",
            str(config.jobs),
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
        if nor is not None:
            command += ["--nor", str(nor.resolve())]
        subprocess.run(command, check=True)
        image.replace(out)


def build_osos(config, out, inputs, recipe_only=False, info=None):
    print("Building OSOS recipe...", flush=True)
    info = info or identity(ROOT)
    recipe = build_recipe(
        ROOT / "payload",
        config.work / "payload" / config.target_path.stem,
        config.target_path,
        config.prefix,
        config.jobs,
        info["revision"],
        info["version"],
    )
    files = recipe.save(out, "osos")
    if not recipe_only:
        assemble(config, files, out / "osos-cfw.bin", inputs)


def build_apple(config, out, inputs, recipe_only=False, nor=None):
    print("Building Apple companion recipe...", flush=True)
    recipe = build_companion_recipe(
        ROOT / "loader/apple",
        config.work / "companion" / config.target_path.stem,
        config.target_path,
        config.prefix,
        config.jobs,
    )
    files = recipe.save(out, "companion")
    if not recipe_only:
        assemble(config, files, out / "cfw-loader.bin", inputs, nor=nor)


def build_rockbox(config, out, rockbox):
    target, prefix, jobs = config.target, config.prefix, config.jobs
    print("Building Rockbox bootloader...", flush=True)
    directory = config.work / "rockbox"
    directory.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = str(Path(prefix).parent) + os.pathsep + env.get("PATH", "")
    env["VERSION"] = target["rockbox_version"]
    makefile = directory / "Makefile"
    configuration = {
        "source": str(rockbox.resolve()),
        "prefix": prefix,
        "version": target["rockbox_version"],
    }
    stamp = directory / "configuration.json"
    if not stamp.is_file() or json.loads(stamp.read_text()) != configuration:
        shutil.rmtree(directory)
        directory.mkdir()
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
    stamp.write_text(json.dumps(configuration, sort_keys=True) + "\n")
    (directory / "build.log").write_text(run(["make", f"-j{jobs}"], directory, env))
    shutil.copyfile(
        directory / "bootloader-ipod6g.ipod", out / "bootloader-ipod6g.ipod"
    )

    packager = config.work / "packager"
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
    parser.add_argument("--inputs", type=Path, default=ROOT / "inputs/firmware-2.0.5")
    parser.add_argument("--out", type=Path, default=ROOT / "build")
    parser.add_argument(
        "--nor", type=Path, help="Device NOR backup for companion personalization"
    )
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
    try:
        target = firmware.load()
    except (OSError, ValueError) as error:
        parser.error(str(error))
    target_path = firmware.PROFILE
    if (
        args.target in ("all", "osos", "osos-recipe")
        and not target_path.with_name("ui.json").is_file()
    ):
        parser.error("Missing native UI metadata")
    if args.target in ("all", "loader") and args.nor is None:
        parser.error("--nor is required to personalize the companion")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    try:
        prefix = compiler_prefix(args.cross_prefix, target["toolchain"]["gcc"])
    except ValueError as error:
        parser.error(str(error))
    config = BuildConfig(target_path, ROOT / ".build", prefix, args.jobs)
    if args.target in ("all", "osos", "osos-recipe"):
        build_osos(
            config,
            out,
            args.inputs,
            args.target == "osos-recipe",
            identity(ROOT, args.tag),
        )
    if args.target in ("all", "loader", "loader-recipe"):
        build_apple(config, out, args.inputs, args.target == "loader-recipe", args.nor)
    if args.target in ("all", "bootloader"):
        dependencies = json.loads((ROOT / "dependencies.lock").read_text())
        rockbox = setup.verify("rockbox", dependencies["rockbox"])
        build_rockbox(config, out, rockbox)
    print(f"Built {args.target}: {out}")


if __name__ == "__main__":
    main()
