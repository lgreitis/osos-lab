#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the NES port, optionally adding ROMs from a local directory."""

import argparse
import struct
import sys
import zipfile
from pathlib import Path

from setup import COMMIT, SHA256, URL

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent / "game-sdk"))
from game_package import manifest, validate_image, write_entry  # noqa: E402


def package(output, rom_directory=None):
    image = (ROOT / "build/nes.eapp").read_bytes()
    validate_image(image)
    artwork = (ROOT / "assets/NES.raw.lcd5").read_bytes()
    if len(artwork) != 6176 or artwork[:16] != struct.pack(
        "<III4s", 55, 55, 112, b"565L"
    ):
        raise ValueError("Expected 55x55 native RGB565 artwork")
    contents = {
        "nes.eapp": image,
        "NES.raw.lcd5": artwork,
        "COPYING.LUCIDE": (ROOT / "assets/COPYING.LUCIDE").read_bytes(),
        "COPYING.INFONES": (ROOT / "vendor/infones/LICENSE").read_bytes(),
        "COPYING.REPRISEOS": (ROOT.parent / "COPYING").read_bytes(),
        "SOURCES.txt": (
            f"InfoNES: {URL}\nCommit: {COMMIT}\nSHA-256: {SHA256}\n"
            "Icon: https://lucide.dev/icons/gamepad (ISC, recolored)\n"
            "Port, SDK, core and integration patch: source/ in this ZIP\n"
        ).encode(),
    }
    rom_paths = []
    if rom_directory:
        rom_paths = sorted(
            p
            for p in rom_directory.iterdir()
            if p.is_file() and not p.name.startswith(".") and p.suffix.lower() == ".nes"
        )
        if not 1 <= len(rom_paths) <= 64:
            raise ValueError("Select a directory containing 1 to 64 NES ROMs")
        names = set()
        for path in rom_paths:
            name = f"roms/{path.name}"
            if (
                any(c in path.name for c in "\\:\r\n")
                or len(f"iPod_Control/games_RO/reprise-nes/{name}".encode("utf-8"))
                >= 258
            ):
                raise ValueError(f"Unsupported ROM filename: {path.name}")
            if name.casefold() in names:
                raise ValueError(f"Duplicate ROM filename on FAT: {path.name}")
            names.add(name.casefold())
            if path.stat().st_size > 4 * 1024 * 1024 + 528:
                raise ValueError(f"ROM exceeds the port's 4 MiB limit: {path.name}")
            contents[name] = path.read_bytes()
    contents["Manifest.plist"] = manifest(
        "NES", "reprise-nes", "nes.eapp", "NES.raw.lcd5", contents, 5120
    )
    sources = []
    for root in (ROOT, ROOT.parent / "game-sdk"):
        for path in sorted(root.rglob("*")):
            relative = path.relative_to(root)
            if not path.is_file() or any(
                p in {"build", ".git", "__pycache__"} for p in relative.parts
            ):
                continue
            if path.suffix.lower() in {".zip", ".nes"}:
                continue
            if (
                relative.parts[0] == "vendor"
                and path.suffix.lower() not in {".cpp", ".h", ".md"}
                and path.name not in {"LICENSE", ".source-sha256"}
            ):
                continue
            sources.append((Path("source") / root.name / relative, path))
    output = output.resolve()
    if output in {p.resolve() for _, p in sources} | {
        p.resolve() for p in rom_paths
    } | {(ROOT / "build/nes.eapp").resolve()}:
        raise ValueError("Output must not overwrite an input or source")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w") as bundle:
        write_entry(bundle, "source/COPYING", (ROOT.parent / "COPYING").read_bytes())
        for name, data in contents.items():
            write_entry(bundle, f"iPod_Control/games_RO/reprise-nes/{name}", data)
        for name, path in sources:
            write_entry(bundle, name.as_posix(), path.read_bytes())
    print(f"Packaged {output} ({len(image)} executable bytes, {len(rom_paths)} ROMs)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/nes.zip")
    parser.add_argument("--rom-directory", type=Path)
    args = parser.parse_args()
    package(args.output, args.rom_directory)
