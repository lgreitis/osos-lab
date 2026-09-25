#!/usr/bin/env python3
"""Fetch pinned Doom/newlib sources, build local libc, and optionally fetch shareware."""

import argparse
import hashlib
import io
import os
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SOURCES = {
    "doomgeneric": (
        "https://codeload.github.com/ozkl/doomgeneric/tar.gz/dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284",
        "1bd3f7f26220494159a38d71f2847ec81b58d6bbd7c7c8d81b08993018001148",
    ),
    "newlib": (
        "https://sourceware.org/pub/newlib/newlib-4.5.0.20241231.tar.gz",
        "33f12605e0054965996c25c1382b3e463b0af91799001f5bb8c0630f2ec8c852",
    ),
}
WAD_URL = "https://www.jbserver.com/downloads/games/doom/misc/shareware/doom1.wad.zip"
WAD_SHA256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"


def download(url):
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()


def fetch_source(name, url, digest):
    destination = ROOT / "vendor" / name
    stamp = destination / ".source-sha256"
    if stamp.exists() and stamp.read_text().strip() == digest:
        return
    if destination.exists():
        raise RuntimeError(f"Unrecognized source directory: {destination}")

    data = download(url)
    if hashlib.sha256(data).hexdigest() != digest:
        raise RuntimeError(f"Source checksum mismatch: {name}")

    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        for member in archive.getmembers():
            parts = Path(member.name).parts[1:]
            if not member.isfile() or not parts:
                continue
            if ".." in parts or Path(*parts).is_absolute():
                raise RuntimeError("Unsafe source archive path")

            target = destination.joinpath(*parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(member) as source, target.open("wb") as output:
                shutil.copyfileobj(source, output)
            target.chmod(member.mode & 0o777)

    stamp.write_text(digest + "\n")


def build_newlib(cross_prefix, jobs):
    compiler = shutil.which(cross_prefix + "gcc")
    if not compiler:
        raise RuntimeError(f"Compiler not found: {cross_prefix}gcc")

    compiler = Path(compiler).resolve()
    target = subprocess.check_output([compiler, "-dumpmachine"], text=True).strip()
    if target != "arm-elf-eabi":
        raise RuntimeError("This recipe requires the workspace arm-elf-eabi toolchain")

    build = ROOT / "build" / "newlib"
    if (build / target / "newlib" / "libc.a").exists():
        return

    build.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ["PATH"])
    configure = [
        str(ROOT / "vendor/newlib/configure"),
        "--target=" + target,
        "--disable-multilib",
        "--disable-newlib-supplied-syscalls",
        "--disable-newlib-multithread",
        "--disable-newlib-io-float",
        "--disable-newlib-wide-orient",
        "--enable-newlib-nano-formatted-io",
        "--disable-nls",
    ]

    with (build / "configure.log").open("w") as log:
        subprocess.run(
            configure,
            cwd=build,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
        )

    with (build / "build.log").open("w") as log:
        subprocess.run(
            ["make", f"-j{jobs}", "all-target-newlib"],
            cwd=build,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
        )


def fetch_shareware():
    path = ROOT / "assets/doom1.wad"
    if path.exists():
        data = path.read_bytes()
    else:
        with zipfile.ZipFile(io.BytesIO(download(WAD_URL))) as archive:
            data = archive.read("DOOM1.WAD")

    if hashlib.sha256(data).hexdigest() != WAD_SHA256:
        raise RuntimeError("Shareware WAD checksum mismatch")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cross-prefix", default="arm-elf-eabi-")
    parser.add_argument("--jobs", type=int, choices=range(1, 9), default=4)
    parser.add_argument("--shareware", action="store_true")
    args = parser.parse_args()

    for name, (url, digest) in SOURCES.items():
        fetch_source(name, url, digest)
    build_newlib(args.cross_prefix, args.jobs)
    if args.shareware:
        fetch_shareware()

    print("Local Doom dependencies ready")
