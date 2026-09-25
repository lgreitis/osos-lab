#!/usr/bin/env python3
"""Package Doom, its shareware episode, and corresponding sources in one ZIP."""

import argparse
import hashlib
import plistlib
import struct
import zipfile
from pathlib import Path

from setup import SOURCES, WAD_SHA256, WAD_URL

ROOT = Path(__file__).resolve().parent
GAME_ID = "reprise-doom"
LOAD_ADDRESS = 0x18000000
MAX_IMAGE_BYTES = 0x100000


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


def write_entry(bundle, name, data):
    entry = zipfile.ZipInfo(str(name), date_time=(2026, 1, 1, 0, 0, 0))
    entry.compress_type = zipfile.ZIP_DEFLATED
    entry.external_attr = 0o100644 << 16
    bundle.writestr(entry, data)


def package(image_path, output, wad_path, artwork_path):
    image = image_path.read_bytes()
    if not 0x28 <= len(image) <= MAX_IMAGE_BYTES:
        raise ValueError("Executable exceeds native image bounds")

    magic, version, count, unknown, imports, *entries = struct.unpack_from(
        "<10I", image
    )
    if (magic, version, count, unknown, imports) != (0x70706165, 0x10001000, 5, 0, 0):
        raise ValueError("Unexpected eapp header")
    for entry in entries:
        if entry % 4 or not LOAD_ADDRESS + 0x28 <= entry < LOAD_ADDRESS + len(image):
            raise ValueError("Callback is outside the ARM executable")

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

    manifest = {
        "Name": "Doom",
        "BuildIdentifier": GAME_ID,
        "GUID": GAME_ID,
        "Version": "0.1.0",
        "HeapSize": 5120,
        "UserDataPath": GAME_ID,
        "Files": [
            {"Path": name, "Size": len(data), "DRM": False, "Verify": False}
            for name, data in contents.items()
        ],
        "Platforms": [
            {
                "PlatformID": 3,
                "PlatformVersion": 1,
                "BuildID": 1,
                "ExecutablePath": "doom.eapp",
                "LaunchingArtwork": "Doom.raw.lcd5",
            }
        ],
    }
    contents["Manifest.plist"] = plistlib.dumps(
        manifest, fmt=plistlib.FMT_XML, sort_keys=False
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
