#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package Doom, its shareware episode, and corresponding sources in one ZIP."""

import argparse
import hashlib
import struct
import sys
import zipfile
from pathlib import Path

from setup import SOURCES, WAD_SHA256, WAD_URL

ROOT = Path(__file__).resolve().parent
GAME_ID = "reprise-doom"
sys.path.insert(0, str(ROOT.parent / "game-sdk"))
from game_package import manifest, validate_image, write_entry  # noqa: E402


def source_files():
    for source_root in (ROOT, ROOT.parent / "game-sdk"):
        for path in sorted(source_root.rglob("*")):
            relative = path.relative_to(source_root)
            if not path.is_file() or path.suffix.lower() == ".wad":
                continue
            if any(part in {"build", "__pycache__", ".git"} for part in relative.parts):
                continue
            if relative.parts[0] == "vendor" and relative.parts[1] != "doomgeneric":
                continue
            if path.suffix.lower() == ".zip":
                continue

            yield Path("source") / source_root.name / relative, path


def package(image_path, output, wad_path, artwork_path):
    image = image_path.read_bytes()
    validate_image(image)

    wad = wad_path.read_bytes()
    if hashlib.sha256(wad).hexdigest() != WAD_SHA256:
        raise ValueError("Expected the pinned Doom 1.9 shareware WAD")

    artwork = artwork_path.read_bytes()
    artwork_header = struct.pack("<III4s", 55, 55, 112, b"565L")
    if len(artwork) != 6176 or artwork[:16] != artwork_header:
        raise ValueError("Expected 55x55 native RGB565 artwork")

    contents = {
        "doom.eapp": image,
        "doom1.wad": wad,
        "Doom.raw.lcd5": artwork,
        "COPYING.REPRISEOS": (ROOT.parent / "COPYING").read_bytes(),
        "COPYING.DOOM": (ROOT / "vendor/doomgeneric/LICENSE").read_bytes(),
        "COPYING.NEWLIB": (ROOT / "vendor/newlib/COPYING.NEWLIB").read_bytes(),
        "SOURCES.txt": (
            f"DoomGeneric source: {SOURCES['doomgeneric'][0]}\n"
            f"Newlib source: {SOURCES['newlib'][0]}\n"
            f"Doom 1.9 shareware episode: {WAD_URL}\n"
            f"WAD SHA-256: {WAD_SHA256}\n"
            "Port, SDK, engine sources and build recipe: source/ in this ZIP\n"
        ).encode(),
    }

    contents["Manifest.plist"] = manifest(
        "Doom", GAME_ID, "doom.eapp", "Doom.raw.lcd5", contents, 5120
    )

    output = output.resolve()
    inputs = [image_path, wad_path, artwork_path]
    sources = list(source_files())
    source_paths = [path for _, path in sources]
    if output in {path.resolve() for path in inputs + source_paths}:
        raise ValueError("Output must not overwrite an input or source file")

    output.parent.mkdir(parents=True, exist_ok=True)
    package_root = Path("iPod_Control/games_RO") / GAME_ID
    with zipfile.ZipFile(output, "w") as bundle:
        write_entry(bundle, "source/COPYING", (ROOT.parent / "COPYING").read_bytes())
        for name, data in contents.items():
            write_entry(bundle, (package_root / name).as_posix(), data)
        for name, path in sources:
            write_entry(bundle, name.as_posix(), path.read_bytes())

    print(f"Packaged {output} ({len(image)} executable bytes)")
    print(f"SHA-256 {hashlib.sha256(output.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "build/doom.eapp")
    parser.add_argument("--output", type=Path, default=ROOT / "build/doom.zip")
    parser.add_argument("--wad", type=Path, default=ROOT / "assets/doom1.wad")
    parser.add_argument("--artwork", type=Path, default=ROOT / "build/Doom.raw.lcd5")
    args = parser.parse_args()

    package(args.image, args.output, args.wad, args.artwork)
