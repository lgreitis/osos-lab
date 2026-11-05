# SPDX-License-Identifier: GPL-3.0-only
"""Compile a theme source manifest into an RTM4/RTH4 theme folder."""

import argparse
import json
import os
import re
import struct
import sys
import tempfile
import zlib
from pathlib import Path, PurePosixPath

from patching.bitmap import read_png
from patching.theme_format import HEADER, MAGIC, RECORD, catalog_hash
from themes.compiler import encode_pixels, fit_palette
from themes import style
from themes.style import COLOR_DEFAULTS, COLOR_ROLES, COLORS, STYLE_BYTES

ROOT = Path(__file__).resolve().parents[1]

METADATA = struct.Struct("<4s32sI32s48s")
METADATA_MAGIC = b"RTM4"
SCHEMA = 1

APPEARANCES = {"light": (0, 1), "dark": (1, 2)}
RENDERING = {"selection": ("native", "flat"), "preview_icons": ("native", "accent")}

# Capabilities recognised by name so authors get a precise answer instead of
# a generic "unknown field". Each one is extended only after native proof.
UNSUPPORTED = {
    "fonts": "custom fonts",
    "layouts": "layout overrides",
    "metrics": "metric overrides",
    "animations": "animations",
    "sounds": "sounds",
    "strings": "string overrides",
    "wallpapers": "wallpapers",
}
SUPPORTED = ("colors", "bitmaps", "accents", "rendering")
THEME_FIELDS = {"schema", "id", "name", "author", "version", "appearances"}
APPEARANCE_FIELDS = set(SUPPORTED)
BITMAP_FIELDS = {"id", "name", "file", "type"}

THEME_ID = re.compile(r"[a-z0-9][a-z0-9_-]{0,30}")
COLOR = re.compile(r"#[0-9A-Fa-f]{6}")
RESOURCE_ID = re.compile(r"0[xX][0-9A-Fa-f]{1,8}|[0-9]{1,10}")
PATH_PART = re.compile(r"[A-Za-z0-9_][A-Za-z0-9._ -]{0,127}")
NAME_BYTES = 47
TEXT_LIMIT = 64
MANIFEST_LIMIT = 256 * 1024
PNG_LIMIT = 16 * 1024 * 1024
PATH_LIMIT = 255


def fail(where, message):
    raise ValueError(f"{where}: {message}" if where else message)


def align(size):
    return size + -size % 4


def strict_json(text, where):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                fail(where, f"duplicate JSON key {key!r}")
            result[key] = value
        return result

    def constant(value):
        fail(where, f"invalid JSON constant {value}")

    try:
        return json.loads(text, object_pairs_hook=pairs, parse_constant=constant)
    except json.JSONDecodeError as error:
        fail(where, f"invalid JSON: {error}")


def check_fields(value, allowed, where):
    if not isinstance(value, dict):
        fail(where, "expected an object")
    for key in sorted(value):
        if key in UNSUPPORTED:
            fail(
                f"{where}.{key}" if where else key,
                f"unsupported capability: {UNSUPPORTED[key]} are not part of "
                f"the RTM4/RTH4 runtime format (supported: {', '.join(SUPPORTED)})",
            )
        if key not in allowed:
            fail(f"{where}.{key}" if where else key, "unknown field")


def text(value, where, limit=TEXT_LIMIT):
    if not isinstance(value, str) or not value:
        fail(where, "expected a non-empty string")
    if any(ord(c) < 32 or ord(c) == 127 for c in value):
        fail(where, "control characters are not allowed")
    if len(value.encode()) > limit:
        fail(where, f"exceeds {limit} UTF-8 bytes")
    return value


def theme_id(value, where="id"):
    if not isinstance(value, str) or not THEME_ID.fullmatch(value):
        fail(where, "expected [a-z0-9][a-z0-9_-]{0,30}")
    if value in ("default", "dark", "classic"):
        fail(where, "reserved theme folder name")
    return value


def catalog_assets(catalog):
    if not isinstance(catalog, dict) or catalog.get("schema") != 1:
        fail("catalog", "expected bitmap catalog schema 1")
    assets = catalog.get("bitmaps")
    if not isinstance(assets, list) or not assets:
        fail("catalog", "expected a non-empty bitmap list")
    return assets


def max_body(catalog):
    """Largest valid body: colors plus one record for every catalog bitmap."""
    return STYLE_BYTES + sum(
        RECORD.size + align(asset["bytes"] - asset["data_offset"])
        for asset in catalog_assets(catalog)
    )


class Resolver:
    def __init__(self, catalog):
        self.assets = catalog_assets(catalog)
        self.by_id = {}
        self.by_name = {}
        for index, asset in enumerate(self.assets):
            if asset["id"] in self.by_id:
                fail("catalog", f"duplicate bitmap id {asset['id']:#010x}")
            self.by_id[asset["id"]] = index
            if asset.get("name"):
                self.by_name.setdefault(asset["name"], []).append(index)

    def __call__(self, entry, where):
        found = []
        if "id" in entry:
            value = entry["id"]
            if isinstance(value, str) and RESOURCE_ID.fullmatch(value):
                value = int(value, 0)
            if isinstance(value, bool) or not isinstance(value, int):
                fail(f"{where}.id", "expected a hex string such as 0x0DAD012A")
            if value not in self.by_id:
                fail(f"{where}.id", f"{value:#010x} is not a catalog bitmap")
            found.append(self.by_id[value])
        if "name" in entry:
            name = entry["name"]
            if not isinstance(name, str) or name not in self.by_name:
                fail(f"{where}.name", f"{name!r} is not a catalog bitmap name")
            if len(self.by_name[name]) > 1:
                fail(f"{where}.name", f"{name!r} is ambiguous; use id")
            found.append(self.by_name[name][0])
        if not found:
            fail(where, "expected id or name")
        if len(set(found)) > 1:
            fail(where, "id and name refer to different bitmaps")
        return found[0]


def source_file(root, value, where):
    if not isinstance(value, str) or not value or len(value) > PATH_LIMIT:
        fail(where, f"expected a relative path of at most {PATH_LIMIT} characters")
    parts = PurePosixPath(value).parts
    if value.startswith("/") or not all(PATH_PART.fullmatch(p) for p in parts):
        fail(where, f"{value!r} must be a relative path of plain file names")
    path = root / Path(*parts)
    try:
        resolved = path.resolve(strict=True)
    except OSError:
        fail(where, f"{value!r} does not exist")
    if not resolved.is_relative_to(root) or not resolved.is_file():
        fail(where, f"{value!r} must be a file inside the manifest directory")
    if resolved.stat().st_size > PNG_LIMIT:
        fail(where, f"{value!r} exceeds {PNG_LIMIT:,} bytes")
    return resolved


def load_manifest(path):
    path = Path(path)
    if path.stat().st_size > MANIFEST_LIMIT:
        fail(path.name, f"exceeds {MANIFEST_LIMIT:,} bytes")
    try:
        source = path.read_bytes().decode("utf-8")
    except UnicodeDecodeError:
        fail(path.name, "expected UTF-8")
    manifest = strict_json(source, path.name)
    check_fields(manifest, THEME_FIELDS, "")
    for key in ("schema", "id", "name", "appearances"):
        if key not in manifest:
            fail(key, "required")
    schema = manifest["schema"]
    if type(schema) is not int or schema != SCHEMA:
        fail("schema", f"unsupported schema {schema!r}; expected {SCHEMA}")
    theme_id(manifest["id"])
    text(manifest["name"], "name", NAME_BYTES)
    for key in ("author", "version"):
        if key in manifest:
            text(manifest[key], key)
    appearances = manifest["appearances"]
    check_fields(appearances, set(APPEARANCES), "appearances")
    if not appearances:
        fail("appearances", "declare light, dark or both")
    for key, entry in appearances.items():
        check_fields(entry, APPEARANCE_FIELDS, f"appearances.{key}")
    return manifest


def resolve_colors(appearance, colors, where):
    if not isinstance(colors, dict):
        fail(where, "expected an object of role -> #RRGGBB")
    result = dict(COLOR_DEFAULTS[appearance])
    for role, value in colors.items():
        if role not in COLOR_ROLES:
            fail(f"{where}.{role}", f"unknown color role; expected one of {COLOR_ROLES}")
        result[role] = color(value, f"{where}.{role}")
    return result


def color(value, where):
    if not isinstance(value, str) or not COLOR.fullmatch(value):
        fail(where, "expected #RRGGBB")
    return value.lower()


def resolve_style(appearance, entry, where):
    """Encode the style prefix; returns its bytes and the report fields."""
    colors = entry.get("colors", {})
    icon = None
    if isinstance(colors, dict) and "icon_tint" in colors:
        colors = dict(colors)
        icon = color(colors.pop("icon_tint"), f"{where}.colors.icon_tint")
    colors = resolve_colors(appearance, colors, f"{where}.colors")

    overrides = entry.get("accents", {})
    check_fields(overrides, set(style.ACCENT_NAMES), f"{where}.accents")
    palettes = {}
    for name in style.ACCENT_NAMES:
        palette = style.accent_defaults(name, appearance, colors["background"])
        override = overrides.get(name, {})
        check_fields(override, set(style.ACCENT_ROLES), f"{where}.accents.{name}")
        for role, value in override.items():
            palette[role] = color(value, f"{where}.accents.{name}.{role}")
        palettes[name] = palette

    rendering = entry.get("rendering", {})
    check_fields(rendering, set(RENDERING), f"{where}.rendering")
    for key, choices in RENDERING.items():
        if rendering.get(key, choices[0]) not in choices:
            fail(f"{where}.rendering.{key}", f"expected {' or '.join(choices)}")
    flags = (style.FLAT_SELECTION if rendering.get("selection") == "flat" else 0) | (
        style.TINT_PREVIEWS if rendering.get("preview_icons") == "accent" else 0
    )

    prefix = style.encode_colors(colors) + style.encode_options(
        flags, icon or colors["accent_text"], palettes
    )
    return prefix, {
        "colors": colors,
        "accents": palettes,
        "rendering": rendering,
        "fallback_accents": [name for name in style.ACCENT_NAMES if name not in overrides],
        "contrast": style.contrast_report(colors, palettes),
    }


def compile_appearance(catalog, resolve, root, appearance, entry):
    where = f"appearances.{appearance}"
    prefix, style_report = resolve_style(appearance, entry, where)
    bitmaps = entry.get("bitmaps", [])
    if not isinstance(bitmaps, list):
        fail(f"{where}.bitmaps", "expected a list")
    selected = {}
    for position, item in enumerate(bitmaps):
        at = f"{where}.bitmaps[{position}]"
        check_fields(item, BITMAP_FIELDS, at)
        if item.get("type", "BMap") != "BMap":
            fail(f"{at}.type", "only BMap replacements are supported")
        if "file" not in item:
            fail(f"{at}.file", "required")
        index = resolve(item, at)
        if index in selected:
            fail(at, f"duplicate replacement for {resolve.assets[index]['name']}")
        selected[index] = (at, source_file(root, item["file"], f"{at}.file"), item)
    body = bytearray(prefix)
    report = []
    for index, (at, path, item) in sorted(selected.items()):
        info = resolve.assets[index]
        try:
            width, height, rgba = read_png(path)
            capacity = info["palette_colors"]
            fitted = fit_palette(rgba, capacity) if capacity else rgba
            payload = encode_pixels(info, width, height, fitted)
        except (KeyError, ValueError, zlib.error, struct.error) as error:
            fail(f"{at} ({item['file']})", str(error) or type(error).__name__)
        if len(payload) != info["bytes"] - info["data_offset"]:
            fail(at, "native encoding changed the bitmap size")
        body.extend(RECORD.pack(index, info["data_offset"], len(payload)))
        body.extend(payload)
        body.extend(bytes(-len(body) % 4))
        report.append(
            {
                "id": f"{info['id']:#010x}",
                "name": info["name"],
                "file": item["file"],
                "bytes": len(payload),
                "method": f"reduced to {capacity} colors"
                if fitted != rgba
                else "native pixels",
            }
        )
    if len(body) > max_body(catalog):
        fail(where, "body exceeds the catalog maximum")
    code = APPEARANCES[appearance][0]
    header = HEADER.pack(
        MAGIC,
        catalog_hash(catalog),
        code,
        len(body),
        len(selected),
        zlib.crc32(body),
    )
    return header + body, {**style_report, "bitmaps": report}


def encode_metadata(catalog, identifier, name, mask):
    return METADATA.pack(
        METADATA_MAGIC,
        catalog_hash(catalog),
        mask,
        identifier.encode("ascii"),
        name.encode(),
    )


def cstring(field, where, decode):
    end = field.find(b"\0")
    if end < 0:
        fail(where, "missing NUL terminator")
    if any(field[end:]):
        fail(where, "bytes after the NUL terminator must be zero")
    try:
        return field[:end].decode(decode)
    except UnicodeDecodeError:
        fail(where, f"invalid {decode}")


def inspect_metadata(data, catalog=None):
    if len(data) != METADATA.size:
        fail("theme.bin", f"expected {METADATA.size} bytes, got {len(data)}")
    magic, fingerprint, mask, raw_id, raw_name = METADATA.unpack(data)
    if magic != METADATA_MAGIC:
        fail("theme.bin", f"bad magic {magic!r}")
    if catalog is not None and fingerprint != catalog_hash(catalog):
        fail("theme.bin", "catalog hash mismatch")
    if mask not in (1, 2, 3):
        fail("theme.bin", f"invalid appearance mask {mask}")
    identifier = theme_id(cstring(raw_id, "theme.bin id", "ascii"), "theme.bin id")
    name = text(
        cstring(raw_name, "theme.bin name", "utf-8"), "theme.bin name", NAME_BYTES
    )
    return {
        "catalog_hash": fingerprint.hex(),
        "appearances": [key for key, (_, bit) in APPEARANCES.items() if mask & bit],
        "id": identifier,
        "name": name,
    }


def inspect_appearance(data, catalog=None, where="pack"):
    if len(data) < HEADER.size + STYLE_BYTES:
        fail(where, f"truncated: {len(data)} bytes")
    magic, fingerprint, code, size, count, crc = HEADER.unpack_from(data)
    if magic != MAGIC:
        fail(where, f"bad magic {magic!r}")
    if catalog is not None and fingerprint != catalog_hash(catalog):
        fail(where, "catalog hash mismatch")
    if code not in (0, 1):
        fail(where, f"invalid appearance {code}")
    if size != len(data) - HEADER.size:
        fail(where, f"body size {size} does not match {len(data) - HEADER.size}")
    if catalog is not None and size > max_body(catalog):
        fail(where, "body exceeds the catalog maximum")
    body = data[HEADER.size :]
    if zlib.crc32(body) != crc:
        fail(where, "CRC32 mismatch")
    colors = {}
    for i, role in enumerate(COLOR_ROLES):
        red, green, blue, alpha = body[i * 4 : i * 4 + 4]
        if alpha != 255:
            fail(where, f"color {role} is not opaque")
        colors[role] = f"#{red:02x}{green:02x}{blue:02x}"
    assets = catalog_assets(catalog) if catalog is not None else None
    records = []
    try:
        options = style.decode_options(body[COLORS:STYLE_BYTES])
    except ValueError as error:
        fail(where, str(error))
    at = STYLE_BYTES
    previous = -1
    for _ in range(count):
        if at + RECORD.size > len(body):
            fail(where, "truncated record header")
        index, offset, length = RECORD.unpack_from(body, at)
        if index <= previous:
            fail(where, "records must be sorted by unique asset index")
        previous = index
        if assets is not None:
            if index >= len(assets):
                fail(where, f"asset index {index} is outside the catalog")
            info = assets[index]
            if (offset, length) != (
                info["data_offset"],
                info["bytes"] - info["data_offset"],
            ):
                fail(where, f"record {index} does not match the native bitmap")
        at += RECORD.size
        end = at + length
        if not length or end + -end % 4 > len(body):
            fail(where, "truncated record payload")
        if assets is not None and info["palette_colors"]:
            pixel_start = at + info["pixels_offset"] - info["data_offset"]
            width = info["depth"] // 8
            for y in range(info["height"]):
                for x in range(info["width"]):
                    pos = pixel_start + y * info["stride"] + x * width
                    value = int.from_bytes(body[pos : pos + width], "little")
                    if value >= info["palette_colors"]:
                        fail(where, f"record {index} has an invalid palette index")
        if any(body[end : end + -end % 4]):
            fail(where, "record padding must be zero")
        records.append({"index": index, "offset": offset, "bytes": length})
        at = align(end)
    if at != len(body):
        fail(where, "trailing bytes after the last record")
    return {
        "catalog_hash": fingerprint.hex(),
        "appearance": "dark" if code else "light",
        "colors": colors,
        "records": records,
        **options,
    }


def validate_package(files, catalog):
    """Check a complete theme folder (name -> bytes) against the catalog."""
    if "theme.bin" not in files:
        fail("", "missing theme.bin")
    metadata = inspect_metadata(files["theme.bin"], catalog)
    expected = {f"{key}.bin" for key in metadata["appearances"]}
    present = {name for name in files if name in ("light.bin", "dark.bin")}
    if present != expected:
        fail(
            "theme.bin",
            f"appearance mask declares {sorted(expected)}, folder has {sorted(present)}",
        )
    packs = {}
    for name in sorted(expected):
        pack = inspect_appearance(files[name], catalog, name)
        if f"{pack['appearance']}.bin" != name:
            fail(name, f"declares the {pack['appearance']} appearance")
        packs[name] = pack
    return {"metadata": metadata, "packs": packs}


def compile_package(catalog, manifest_path):
    manifest_path = Path(manifest_path)
    manifest = load_manifest(manifest_path)
    root = manifest_path.parent.resolve()
    resolve = Resolver(catalog)
    files = {}
    appearances = {}
    mask = 0
    for key in ("light", "dark"):
        if key not in manifest["appearances"]:
            continue
        pack, detail = compile_appearance(
            catalog, resolve, root, key, manifest["appearances"][key]
        )
        files[f"{key}.bin"] = pack
        detail["file_bytes"] = len(pack)
        appearances[key] = detail
        mask |= APPEARANCES[key][1]
    files["theme.bin"] = encode_metadata(
        catalog, manifest["id"], manifest["name"], mask
    )
    validate_package(files, catalog)
    report = {
        "format": {
            "metadata": METADATA_MAGIC.decode(),
            "appearance": MAGIC.decode(),
            "source_schema": SCHEMA,
        },
        "id": manifest["id"],
        "name": manifest["name"],
        "source": {k: manifest[k] for k in ("author", "version") if k in manifest},
        "catalog_hash": catalog_hash(catalog).hex(),
        "capabilities": {
            "supported": [
                "colors: 16 opaque roles",
                "bitmaps: same-size BMap",
                "named accent palettes",
                "flat selection",
                "preview icon tint",
            ],
            "unsupported": sorted(UNSUPPORTED),
        },
        "appearances": appearances,
        "memory_estimate": {
            "note": "Estimated from file sizes, not measured on device RAM.",
            "metadata_bytes": METADATA.size,
            "color_table_bytes_per_appearance": STYLE_BYTES,
            "largest_appearance_bytes": max(
                len(files[f"{key}.bin"]) for key in appearances
            ),
            "bitmap_payload_bytes": {
                key: sum(item["bytes"] for item in detail["bitmaps"])
                for key, detail in appearances.items()
            },
        },
    }
    files["report.json"] = (
        json.dumps(report, indent=2, sort_keys=True) + "\n"
    ).encode()
    return dict(sorted(files.items()))


def write_package(files, out):
    """Stage every file, then replace outputs and drop stale appearance packs."""
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    staged = []
    try:
        for name, data in files.items():
            handle, temporary = tempfile.mkstemp(prefix=f".{name}.", dir=out)
            staged.append((temporary, out / name))
            with os.fdopen(handle, "wb") as stream:
                stream.write(data)
            os.chmod(temporary, 0o644)
        for temporary, target in staged:
            os.replace(temporary, target)
    finally:
        for temporary, _ in staged:
            if os.path.exists(temporary):
                os.unlink(temporary)
    for name in ("light.bin", "dark.bin"):
        if name not in files and (out / name).is_file():
            (out / name).unlink()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, default=ROOT / "firmware/bitmaps.json")
    args = parser.parse_args(argv)
    try:
        catalog = json.loads(args.catalog.read_text())
        files = compile_package(catalog, args.manifest)
        write_package(files, args.out)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    for name, data in files.items():
        print(f"{name}: {len(data):,} bytes")
    print(f"Wrote {args.out}")


if __name__ == "__main__":
    sys.exit(main())
