#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Adapted from RepriseOS game-sdk/game_package.py and doom/artwork.py (8fc840d) by lgreitis.
"""Pack an eApp image into the package folder Apple's host loads.

    eapp-pack --kind game --id hello --name Hello --image build/hello.eapp \\
        --icon icon.png --symbols firmware/osos-2.0.5.h --heap-kib 5120 \\
        --out build [FILE ...]

writes build/hello/ with Manifest.plist, hello.eapp, hello.raw.lcd5 (the 55 x 55
RGB565 icon from the PNG), every FILE under its relative path, which is the
name the app reads it by (sounds/beep.wav stays sounds/beep.wav), and every
--picture FORMAT PNG as an image file (art/tiles.png becomes art/tiles.eimg),
every --pack NAME FORMAT PNG ... as one NAME.epak holding those pictures;
--images-header writes a C header with their names, indexes and sizes. The
folder goes to /Reprise/Games/hello/ (--kind game) or /Reprise/Apps/hello/
(--kind app). --zip also writes build/hello.zip with the folder under that root.
--names names.toml gives the name per language, one `locale = "name"` line
each; the host lists that name when the iPod's language matches and --name
otherwise. Every --module NAME ELF, a module linked at 0 against the image
--elf, becomes NAME.emod; --prefix names the cross tools that read it.
--module-externs LDSCRIPT OBJECT ... writes the linker script that keeps in
the image what those module objects use of it, leaving out what they and
--libgcc define, and stops.

The image checks come from the SDK's symbols header (--symbols). The module is
importable: validate_image, manifest, read_names, artwork and write_entry serve
an app with a packaging script of its own.
"""

import argparse
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

ROOTS = {"game": "Reprise/Games", "app": "Reprise/Apps"}
ICON_SIDE = 55
ICON_STRIDE = ICON_SIDE * 2 + 2
VERSION = "1.0"  # The package's Version when the app names none.
# The locales of the Settings > Language list, which the host compares with
# the keys of LocalizedNames.
LOCALES = (
    "en_US", "da_DK", "de_CH", "de_DE", "es_ES", "fi_FI", "fr_BE", "fr_CH", "fr_FR",
    "it_IT", "ja_JP", "ko_KR", "nl_BE", "nl_NL", "no_NO", "pt_PT", "sv_SE", "zh_CN",
    "zh_TW", "cs_CZ", "el_GR", "hu_HU", "pl_PL", "ru_RU", "tr_TR",
)  # fmt: skip


IMAGE_CONSTANTS = (
    "GameImage_Magic",
    "GameImage_MaxVersion",
    "GameImage_MaxEntries",
    "GameImage_LoadAddress",
    "GameImage_MaxBytes",
    "GameImageHeader_entries",
    "GameImageHeader_bytes",
    "GameImport_Tail",
)


def image_constants(symbols):
    """What the host checks in an image, from the symbols header `symbols`."""
    text = Path(symbols).read_text()
    found = dict(
        re.findall(r"#define (GameImage\w+|GameImport_Tail) (0x[0-9a-fA-F]+)", text)
    )
    missing = [name for name in IMAGE_CONSTANTS if name not in found]
    if missing:
        raise ValueError(f"{symbols} lacks {', '.join(missing)}")
    return {name: int(found[name], 16) for name in IMAGE_CONSTANTS}


def validate_image(image, symbols):
    """Raise ValueError unless `image` is an eApp image the host accepts."""
    c = image_constants(symbols)
    header_bytes = c["GameImageHeader_bytes"]
    if not header_bytes <= len(image) <= c["GameImage_MaxBytes"]:
        raise ValueError("image exceeds the eApp bounds")
    magic, version, count, reserved, imports = struct.unpack_from("<5I", image)
    entries = struct.unpack_from(
        f"<{c['GameImage_MaxEntries']}I", image, c["GameImageHeader_entries"]
    )
    if (magic, version, count, reserved) != (
        c["GameImage_Magic"],
        c["GameImage_MaxVersion"],
        c["GameImage_MaxEntries"],
        0,
    ):
        raise ValueError("unexpected eapp header")
    base = c["GameImage_LoadAddress"]
    for entry in entries:
        if entry % 4 or not base + header_bytes <= entry < base + len(image):
            raise ValueError("an entry lies outside the image")
    validate_imports(image, base, imports, c["GameImport_Tail"])


IMPORT_RECORD = struct.Struct("<32s4I I I")
STUB = struct.Struct("<II")
STUB_JUMP = 0xE51FF004  # ldr pc, [pc, #-4], as runtime/imports.S emits a stub.
IMPORTS_END = b"$$$$ a^n + b^n = c^n | n>2 $$$$"


def validate_imports(image, base, imports, marker):
    """Raise ValueError unless the import list at `imports` is one the host
    walks to its end: records of 32-byte name, interface id, count, next and
    count stubs, each a jump through the word after it, under the host's
    tail `marker`."""
    seen = set()
    while imports:
        offset = imports - base
        if (
            imports % 4
            or offset in seen
            or not 0 <= offset <= len(image) - IMPORT_RECORD.size
        ):
            raise ValueError("an import record lies outside the image")
        seen.add(offset)
        name, *_, count, imports = IMPORT_RECORD.unpack_from(image, offset)
        name = name.rstrip(b"\0")
        if not name:
            raise ValueError("an import record has no interface name")
        stubs = offset + IMPORT_RECORD.size
        end = stubs + count * STUB.size
        if end + 4 > len(image):
            raise ValueError(f"the {name.decode()} import stubs exceed the image")
        for i in range(count):
            instruction, word = STUB.unpack_from(image, stubs + i * STUB.size)
            if instruction != STUB_JUMP or word:
                raise ValueError(f"the {name.decode()} import stubs are malformed")
        if struct.unpack_from("<I", image, end)[0] != marker:
            raise ValueError(f"the {name.decode()} import record lacks its marker")
        if name == IMPORTS_END:
            return
    raise ValueError("the import list does not end")


def check_names(names):
    """Raises ValueError for a locale the host does not know or an empty name."""
    for locale, name in names.items():
        if locale not in LOCALES:
            raise ValueError(f"{locale} is not a locale of Settings > Language")
        if not isinstance(name, str) or not name.strip() or "\0" in name:
            raise ValueError(f"the name for {locale} must be a non-empty string")


def read_names(path):
    """The names per locale of a names.toml file, checked."""
    import tomllib  # Python 3.11, needed only for --names

    with open(path, "rb") as file:
        try:
            names = tomllib.load(file)
        except tomllib.TOMLDecodeError as error:
            raise ValueError(f"{path}: {error}") from None
    check_names(names)
    return names


def manifest(
    name,
    package_id,
    executable,
    artwork,
    contents,
    heap_kib,
    version=VERSION,
    localized_names=None,
):
    """Manifest.plist as XML: `contents` maps a path to its bytes,
    `localized_names` a locale to the name listed in that language."""
    platform = {
        "PlatformVersion": 1,
        "BuildID": 1,
        "ExecutablePath": executable,
        "LaunchingArtwork": artwork,
    }
    names = {"LocalizedNames": dict(localized_names)} if localized_names else {}
    return plistlib.dumps(
        {
            "Name": name,
            **names,
            "BuildIdentifier": package_id,
            "GUID": package_id,
            "Version": version,
            "HeapSize": heap_kib,
            "UserDataPath": package_id,
            "Files": [
                {"Path": path, "Size": len(data), "DRM": False, "Verify": False}
                for path, data in contents.items()
            ],
            # The nano 3G (2) and the classic (3) run the same image; the
            # nano is untested.
            "Platforms": [{"PlatformID": 2, **platform}, {"PlatformID": 3, **platform}],
        },
        fmt=plistlib.FMT_XML,
        sort_keys=False,
    )


def write_entry(bundle, name, data):
    """A reproducible ZIP entry."""
    entry = zipfile.ZipInfo(str(name), date_time=(2026, 1, 1, 0, 0, 0))
    entry.compress_type = zipfile.ZIP_DEFLATED
    entry.external_attr = 0o100644 << 16
    bundle.writestr(entry, data)


def _paeth(a, b, c):
    p = a + b - c
    return min((a, b, c), key=lambda value: abs(p - value))


def decode_png(png, what="the PNG"):
    """An 8-bit RGB or RGBA, non-interlaced PNG as (width, height, rows), each
    row a bytearray of RGBA bytes."""
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{what} is not a PNG")
    offset, channels, width, height = 8, 0, 0, 0
    compressed = bytearray()
    while offset < len(png):
        length, kind = struct.unpack_from(">I4s", png, offset)
        data = png[offset + 8 : offset + 8 + length]
        if (
            zlib.crc32(kind + data)
            != struct.unpack_from(">I", png, offset + 8 + length)[0]
        ):
            raise ValueError("PNG checksum mismatch")
        offset += length + 12
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = (
                struct.unpack(">IIBBBBB", data)
            )
            fields = (depth, compression, filtering, interlace)
            if fields != (8, 0, 0, 0) or color not in (2, 6):
                raise ValueError(
                    f"{what} must be a non-interlaced 8-bit RGB or RGBA PNG"
                )
            channels = 4 if color == 6 else 3
        elif kind == b"IDAT":
            compressed.extend(data)
        elif kind == b"IEND":
            break
    if not channels:
        raise ValueError("PNG header missing")
    pixels = zlib.decompress(compressed)
    stride = width * channels
    if len(pixels) != height * (stride + 1):
        raise ValueError("unexpected PNG pixel length")
    rows = []
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        filter_type = pixels[start]
        if filter_type > 4:
            raise ValueError("unsupported PNG row filter")
        row = bytearray(pixels[start + 1 : start + 1 + stride])
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            above = previous[i]
            corner = previous[i - channels] if i >= channels else 0
            predictors = (
                0,
                left,
                above,
                (left + above) // 2,
                _paeth(left, above, corner),
            )
            row[i] = (row[i] + predictors[filter_type]) & 255
        previous = row
        if channels == 3:
            rgba = bytearray()
            for x in range(width):
                rgba.extend(row[x * 3 : x * 3 + 3])
                rgba.append(255)
            row = rgba
        rows.append(row)
    return width, height, rows


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def artwork(png):
    """A 55 x 55, 8-bit RGB or RGBA, non-interlaced PNG as the host's RGB565
    artwork: a 16-byte header and the rows, transparent pixels over black."""
    width, height, rows = decode_png(png, "the icon")
    if (width, height) != (ICON_SIDE, ICON_SIDE):
        raise ValueError("the icon must be 55 x 55")
    result = bytearray(
        struct.pack("<III4s", ICON_SIDE, ICON_SIDE, ICON_STRIDE, b"565L")
    )
    for row in rows:
        for x in range(ICON_SIDE):
            p = x * 4
            alpha = row[p + 3]
            r, g, b = ((value * alpha + 127) // 255 for value in row[p : p + 3])
            result.extend(struct.pack("<H", rgb565(r, g, b)))
        result.extend(b"\0\0")
    return bytes(result)


# An image file of the package: a 20-byte header, "EIMG", width, height, the
# format and the colour key as little-endian words, then the pixels row by
# row without padding. The runtime's eapp_bitmap_load reads the 565 form; the
# others are OpenGL ES texture data.
IMAGE_MAGIC = b"EIMG"
IMAGE_FORMATS = {"565": 0, "4444": 1, "5551": 2, "8888": 3, "a8": 4}
# A colour no PNG pixel converts to, as the key of an image with transparent
# pixels: magenta's two low bits are not RGB565.
IMAGE_KEY = 0xF81F
# The longest side the runtime's loaders take.
IMAGE_MAX_SIDE = 4096


def picture(png, fmt, what="the image"):
    """A PNG as an image file in `fmt`. 565 is opaque pixels with a colour key
    taking the place of pixels of alpha below 128 (-1 for none); the other
    formats keep the alpha."""
    if fmt not in IMAGE_FORMATS:
        raise ValueError(
            f"{what}: the format must be one of {', '.join(IMAGE_FORMATS)}"
        )
    width, height, rows = decode_png(png, what)
    if width > IMAGE_MAX_SIDE or height > IMAGE_MAX_SIDE:
        raise ValueError(
            f"{what}: {width} x {height} exceeds {IMAGE_MAX_SIDE} pixels a side"
        )
    pixels = bytearray()
    transparent = any(row[x * 4 + 3] < 128 for row in rows for x in range(width))
    key = IMAGE_KEY if fmt == "565" and transparent else -1
    for row in rows:
        for x in range(width):
            r, g, b, a = row[x * 4 : x * 4 + 4]
            if fmt == "565":
                word = IMAGE_KEY if a < 128 else rgb565(r, g, b)
                if a >= 128 and transparent and word == IMAGE_KEY:
                    word ^= 0x0020  # The nearest colour that is not the key.
                pixels.extend(struct.pack("<H", word))
            elif fmt == "4444":
                pixels.extend(
                    struct.pack(
                        "<H", (r >> 4) << 12 | (g >> 4) << 8 | (b >> 4) << 4 | a >> 4
                    )
                )
            elif fmt == "5551":
                pixels.extend(
                    struct.pack(
                        "<H", (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | a >> 7
                    )
                )
            elif fmt == "8888":
                pixels.extend(bytes((r, g, b, a)))
            else:
                pixels.append(a)
    header = struct.pack("<4sIIIi", IMAGE_MAGIC, width, height, IMAGE_FORMATS[fmt], key)
    return header + bytes(pixels), width, height


def image_name(path):
    """The package name of a picture: its relative path with .eimg."""
    return Path(path).with_suffix(".eimg").as_posix()


def macro_name(path):
    stem = Path(path).stem
    return "".join(c if c.isalnum() else "_" for c in stem).upper()


# A pack: "EPAK", the entry count and an offset per entry as little-endian
# words, then the pictures, each 4-byte aligned.
PACK_MAGIC = b"EPAK"
# The largest pack and entry count eapp_pack_load takes.
PACK_MAX_BYTES = 4 << 20
PACK_MAX_ENTRIES = 1024


def pack_file(pictures, what="the pack"):
    """The pictures (from convert_pictures) as one pack file."""
    if len(pictures) > PACK_MAX_ENTRIES:
        raise ValueError(f"{what}: {len(pictures)} pictures exceed {PACK_MAX_ENTRIES}")
    table = 8 + 4 * len(pictures)
    offsets, body = [], bytearray()
    for _, data, _, _ in pictures:
        offsets.append(table + len(body))
        body += data
        body += b"\0" * (-len(body) % 4)
    data = (
        struct.pack("<4sI", PACK_MAGIC, len(pictures))
        + struct.pack(f"<{len(pictures)}I", *offsets)
        + bytes(body)
    )
    if len(data) > PACK_MAX_BYTES:
        raise ValueError(f"{what}: {len(data)} bytes exceed {PACK_MAX_BYTES}")
    return data


def images_header(pictures, packs=()):
    """A C header naming each loose picture's package path and size,
    EAPP_IMAGE_<STEM>, _WIDTH and _HEIGHT, and each pack's file,
    EAPP_PACK_<NAME>, with EAPP_PACK_<NAME>_<STEM> as the entry's index and
    the entry's EAPP_IMAGE_<STEM>_WIDTH and _HEIGHT."""
    lines = ["/* Generated by eapp-pack from the Makefile's images. */", ""]
    names = {}

    def size_lines(path, width, height):
        name = macro_name(path)
        if name in names:
            raise ValueError(f"{path}: EAPP_IMAGE_{name} is also {names[name]}'s name")
        names[name] = path
        return name, [
            f"#define EAPP_IMAGE_{name}_WIDTH {width}",
            f"#define EAPP_IMAGE_{name}_HEIGHT {height}",
        ]

    for path, _, width, height in pictures:
        name, sizes = size_lines(path, width, height)
        lines += [f'#define EAPP_IMAGE_{name} "{image_name(path)}"', *sizes]
    for pack_name, entries in packs:
        pack_macro = macro_name(pack_name)
        lines += ["", f'#define EAPP_PACK_{pack_macro} "{pack_name}.epak"']
        for index, (path, _, width, height) in enumerate(entries):
            name, sizes = size_lines(path, width, height)
            lines += [f"#define EAPP_PACK_{pack_macro}_{name} {index}", *sizes]
    return "\n".join(lines) + "\n"


def convert_pictures(specs):
    """(path, format) pairs to (path, data, width, height)."""
    pictures = []
    for path, fmt in specs:
        data, width, height = picture(Path(path).read_bytes(), fmt, str(path))
        pictures.append((path, data, width, height))
    return pictures


def read_packs(args):
    """--pack arguments, each NAME FORMAT PNG [FORMAT PNG ...], to
    (name, pictures) pairs."""
    packs = []
    for spec in args:
        name, *rest = spec
        if not rest or len(rest) % 2:
            raise ValueError(f"--pack {name}: FORMAT PNG pairs expected")
        pairs = [(Path(rest[i + 1]), rest[i]) for i in range(0, len(rest), 2)]
        packs.append((name, convert_pictures(pairs)))
    return packs


# A module: "EMOD", the version, the image's 16-byte build id, the bytes of
# text and data, the bytes of bss and the relocation count as little-endian
# words, then text and data linked at 0, then the relocation offsets.
MODULE_MAGIC = b"EMOD"
MODULE_VERSION = 1
MODULE_HEADER = struct.Struct("<4sI16sIII")
# The largest block eapp_alloc gives, which holds a loaded module's text,
# data and bss.
MODULE_MAX_BYTES = (8 << 20) - 8
# A module's sections may ask for no more than a heap block's alignment.
MODULE_MAX_ALIGN = 8
MODULE_RELOCATION_SECTIONS = {".rel.text", ".rel.data", ".rela.text", ".rela.data"}
# Position-independent within the module; nothing to fix up at load.
MODULE_RELATIVE_RELOCATIONS = {
    "R_ARM_CALL",
    "R_ARM_JUMP24",
    "R_ARM_THM_CALL",
    "R_ARM_THM_JUMP24",
    "R_ARM_THM_JUMP11",
    "R_ARM_THM_JUMP8",
    "R_ARM_PREL31",
    "R_ARM_NONE",
    "R_ARM_V4BX",  # marks a bx for ARMv4 linkers; libgcc's division helpers carry it
}


def run_tool(command):
    return subprocess.check_output([str(c) for c in command], text=True)


def elf_symbols(symbol_table):
    """`readelf -sW` output as {number: (name, value, bind, visibility,
    section index)}. The image's symbols arrive through --just-symbols with
    the index ABS; a module's static may share a name with one of them."""
    symbols = {}
    for line in symbol_table.splitlines():
        parts = line.split()
        if len(parts) in (7, 8) and parts[0].rstrip(":").isdigit():
            name = parts[7] if len(parts) == 8 else ""
            symbols[int(parts[0].rstrip(":"))] = (
                name,
                int(parts[1], 16),
                parts[4],
                parts[5],
                parts[6],
            )
    return symbols


def module_symbol(symbols, name):
    """The value of the module's own symbol `name`, or None."""
    for entry in symbols.values():
        if entry[0] == name and entry[4] not in ("ABS", "UND"):
            return entry[1]
    return None


def global_names(symbols):
    """The names of the globals `symbols` (elf_symbols) define themselves;
    libgcc's helpers are hidden and left out."""
    return {
        name
        for name, _, bind, visibility, index in symbols.values()
        if bind in ("GLOBAL", "WEAK")
        and visibility == "DEFAULT"
        and index not in ("ABS", "UND")
    }


def check_module_globals(symbols, image_symbols, what="the module"):
    """Raise ValueError when the module defines a global the image also
    defines: the link lets the module use its own and the image its own."""
    for name in sorted(global_names(symbols) & global_names(image_symbols)):
        raise ValueError(f"{what} defines {name}, which the image also defines")


def module_relocations(relocation_table, symbols):
    """The offsets of the R_ARM_ABS32 words of `readelf -rW` output that hold
    an address inside the module, in order. A word naming one of the image's
    absolute symbols stays as linked. A relative relocation naming one only
    holds at the address the module is linked at."""
    offsets = set()
    section = None
    for line in relocation_table.splitlines():
        match = re.match(r"Relocation section '(\S+)'", line)
        if match:
            section = match.group(1)
            continue
        parts = line.split()
        if len(parts) < 3 or not re.fullmatch(r"[0-9a-f]+", parts[0]):
            continue
        if section not in MODULE_RELOCATION_SECTIONS:
            if section.startswith((".rel.debug", ".rela.debug")):
                continue
            raise ValueError(
                f"relocations in {section}, which the loader does not apply"
            )
        kind = parts[2]
        number = int(parts[1], 16) >> 8  # 0: the relocation names no symbol
        if number and number not in symbols:
            raise ValueError(f"relocation at {parts[0]} names unknown symbol {number}")
        name, index = (symbols[number][0], symbols[number][4]) if number else ("", None)
        if index == "UND":
            raise ValueError(
                f"{name} is not defined by the module or the image; "
                "weak references are not supported"
            )
        if kind == "R_ARM_ABS32":
            if index != "ABS":
                if int(parts[0], 16) % 4:
                    raise ValueError(f"the address at {parts[0]} is not word-aligned")
                offsets.add(int(parts[0], 16))
        elif kind not in MODULE_RELATIVE_RELOCATIONS:
            raise ValueError(f"unsupported relocation {kind} at {parts[0]}")
        elif index == "ABS" and kind not in ("R_ARM_NONE", "R_ARM_V4BX"):
            raise ValueError(
                f"{name} of the image is reached with {kind}, a short call that "
                "cannot leave the heap; build the module with -mlong-calls"
            )
    return sorted(offsets)


# The allocated sections runtime/module.lds makes; any other comes from an
# input section the script does not place.
MODULE_SECTIONS = {".text", ".data", ".bss", ".init_array"}


def section_alignments(section_table):
    """{name: alignment} of the allocated sections in `readelf -SW` output."""
    alignments = {}
    for line in section_table.splitlines():
        match = re.match(
            r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+[0-9a-f]+\s+"
            r"[0-9a-f]+\s+[0-9a-f]+\s+(\S*)\s+\d+\s+\d+\s+(\d+)",
            line,
        )
        if match and "A" in match.group(2):
            alignments[match.group(1)] = int(match.group(3))
    return alignments


def build_id(notes):
    """The 16 bytes of `readelf -n` output's GNU build id."""
    match = re.search(r"Build ID: ([0-9a-f]+)", notes)
    if not match or len(match.group(1)) != 32:
        raise ValueError(
            "the image has no 16-byte build id; link it with --build-id=md5"
        )
    return bytes.fromhex(match.group(1))


def module_file(binary, symbols, relocation_table, image_id, what="the module"):
    """The .emod of a module from its raw binary (objcopy -O binary, from 0),
    its symbols (elf_symbols) and relocations, for the image of `image_id`;
    check_module_globals runs before."""
    values = {}
    for name in ("eapp_module", "__eapp_module_bss_start", "__eapp_module_bss_end"):
        values[name] = module_symbol(symbols, name)
        if values[name] is None:
            raise ValueError(f"{what} lacks {name}; link it with runtime/module.lds")
    if values["eapp_module"] != 0:
        raise ValueError(f"{what}: the EAPP_MODULE table must come first")
    image_bytes = values["__eapp_module_bss_start"]
    bss_bytes = values["__eapp_module_bss_end"] - image_bytes
    if image_bytes < len(binary) or image_bytes % 4 or bss_bytes < 0:
        raise ValueError(f"{what}: segment layout is not text, data, bss in order")
    if image_bytes + bss_bytes > MODULE_MAX_BYTES:
        raise ValueError(f"{what}: {image_bytes + bss_bytes} bytes exceed a heap block")
    offsets = module_relocations(relocation_table, symbols)
    if offsets and offsets[-1] > image_bytes - 4:
        raise ValueError(f"{what}: a relocation lies outside text and data")
    image = binary + b"\0" * (image_bytes - len(binary))
    header = MODULE_HEADER.pack(
        MODULE_MAGIC, MODULE_VERSION, image_id, image_bytes, bss_bytes, len(offsets)
    )
    return header + image + struct.pack(f"<{len(offsets)}I", *offsets)


def read_module(prefix, elf, image_id, image_symbols, what):
    """module_file for a linked module ELF, with the cross tools of `prefix`;
    `image_symbols` are the image's (elf_symbols)."""
    sections = section_alignments(run_tool([prefix + "readelf", "-SW", elf]))
    for section, align in sections.items():
        if section not in MODULE_SECTIONS:
            raise ValueError(f"{what}: {section} is a section modules cannot hold")
        if align > MODULE_MAX_ALIGN:
            raise ValueError(f"{what}: {section} asks for {align}-byte alignment")
    with tempfile.TemporaryDirectory() as work:
        raw = Path(work) / "module.bin"
        run_tool([prefix + "objcopy", "-O", "binary", elf, raw])
        binary = raw.read_bytes()
    symbols = elf_symbols(run_tool([prefix + "readelf", "-sW", elf]))
    check_module_globals(symbols, image_symbols, what)
    return module_file(
        binary,
        symbols,
        run_tool([prefix + "readelf", "-rW", elf]),
        image_id,
        what,
    )


def module_externs(undefined, defined):
    """A linker script that keeps in the image every symbol the modules use:
    the names of `undefined` (nm -u of the modules' objects) that neither the
    modules nor their libgcc define."""
    names = sorted(set(undefined) - set(defined))
    lines = ["/* Generated by eapp-pack: what the modules use of the image. */"]
    lines += [f"EXTERN({name})" for name in names]
    return "\n".join(lines) + "\n"


def nm_names(prefix, files, flag):
    names = []
    for line in run_tool([prefix + "nm", flag, *files]).splitlines():
        parts = line.split()
        if parts and not line.endswith(":"):
            names.append(parts[-1])
    return names


def package(
    kind,
    package_id,
    name,
    image,
    icon,
    heap_kib,
    files,
    symbols,
    version=VERSION,
    localized_names=None,
    pictures=(),
    packs=(),
    modules=(),
):
    """The package's files by name, Manifest.plist included. A data file keeps
    its relative path as its name; a picture of `pictures` (from
    convert_pictures) its path with .eimg; a pack of `packs` (from read_packs)
    is <name>.epak; a module of `modules`, (name, .emod bytes) pairs, is
    <name>.emod."""
    if kind not in ROOTS:
        raise ValueError("kind must be game or app")
    check_names(localized_names or {})
    validate_image(image, symbols)
    executable = f"{package_id}.eapp"
    artwork_name = f"{package_id}.raw.lcd5"
    contents = {executable: image, artwork_name: artwork(icon)}
    for path in files:
        path = Path(path)
        entry = path.as_posix()
        if path.is_absolute() or ".." in path.parts:
            raise ValueError(f"{path} must be a plain relative path")
        if entry in contents or entry == "Manifest.plist":
            raise ValueError(f"{entry} is taken")
        contents[entry] = path.read_bytes()
    for path, data, _, _ in pictures:
        entry = image_name(path)
        if entry in contents:
            raise ValueError(f"{entry} is taken")
        contents[entry] = data
    for pack_name, entries in packs:
        entry = f"{pack_name}.epak"
        if entry in contents:
            raise ValueError(f"{entry} is taken")
        contents[entry] = pack_file(entries, entry)
    for module_name, data in modules:
        entry = f"{module_name}.emod"
        if entry in contents:
            raise ValueError(f"{entry} is taken")
        image_bytes, bss_bytes = MODULE_HEADER.unpack_from(data)[3:5]
        if image_bytes + bss_bytes > heap_kib * 1024:
            bytes_ = image_bytes + bss_bytes
            raise ValueError(f"{entry}: {bytes_} bytes exceed the heap of --heap-kib")
        contents[entry] = data
    plist = manifest(
        name,
        package_id,
        executable,
        artwork_name,
        contents,
        heap_kib,
        version,
        localized_names,
    )
    return {"Manifest.plist": plist, **contents}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--kind", choices=sorted(ROOTS))
    parser.add_argument("--id", help="package id, the folder name")
    parser.add_argument("--name", help="what Extras lists")
    parser.add_argument(
        "--names", type=Path, help='names.toml: locale = "name" per language'
    )
    parser.add_argument("--image", type=Path, help="the .eapp image")
    parser.add_argument("--icon", type=Path, help="55 x 55 PNG")
    parser.add_argument(
        "--symbols",
        type=Path,
        help="the SDK's osos-2.0.5.h, for what the host checks in an image",
    )
    parser.add_argument("--heap-kib", type=int)
    parser.add_argument("--version", default=VERSION, help="the package's Version")
    parser.add_argument("--out", type=Path, default=Path("build"))
    parser.add_argument("--zip", action="store_true", help="also write <out>/<id>.zip")
    parser.add_argument(
        "--picture",
        nargs=2,
        action="append",
        default=[],
        metavar=("FORMAT", "PNG"),
        help="a PNG packed as <path>.eimg in 565, 4444, 5551, 8888 or a8",
    )
    parser.add_argument(
        "--pack",
        nargs="+",
        action="append",
        default=[],
        metavar="ARG",
        help="NAME FORMAT PNG [FORMAT PNG ...]: pictures packed as NAME.epak",
    )
    parser.add_argument(
        "--images-header",
        type=Path,
        help="write the C header naming the pictures, packs and their sizes",
    )
    parser.add_argument(
        "--header-only",
        action="store_true",
        help="write --images-header and stop, before the image exists",
    )
    parser.add_argument(
        "--module",
        nargs=2,
        action="append",
        default=[],
        metavar=("NAME", "ELF"),
        help="a linked module packed as NAME.emod; needs --elf and --prefix",
    )
    parser.add_argument(
        "--elf", type=Path, help="the image's ELF, whose build id the modules carry"
    )
    parser.add_argument(
        "--module-externs",
        nargs="+",
        type=Path,
        metavar=("LDSCRIPT", "OBJECT"),
        help="write the linker script that keeps what the modules' objects use "
        "in the image, and stop",
    )
    parser.add_argument("--libgcc", type=Path, help="libgcc.a, which modules link")
    parser.add_argument("--prefix", default="", help="the cross tools' prefix")
    parser.add_argument("files", type=Path, nargs="*", help="data files the app reads")
    args = parser.parse_args()
    try:
        if args.module_externs:
            script, *objects = args.module_externs
            defined = nm_names(args.prefix, objects, "--defined-only")
            if args.libgcc:
                defined += nm_names(args.prefix, [args.libgcc], "--defined-only")
            text = module_externs(nm_names(args.prefix, objects, "-u"), defined)
            if not script.exists() or script.read_text() != text:
                script.parent.mkdir(parents=True, exist_ok=True)
                script.write_text(text)
            return 0
        pictures = convert_pictures((Path(png), fmt) for fmt, png in args.picture)
        packs = read_packs(args.pack)
        if args.images_header:
            args.images_header.parent.mkdir(parents=True, exist_ok=True)
            args.images_header.write_text(images_header(pictures, packs))
        if args.header_only:
            if not args.images_header:
                parser.error("--header-only needs --images-header")
            return 0
        missing = [
            flag
            for flag, value in (
                ("--kind", args.kind),
                ("--id", args.id),
                ("--name", args.name),
                ("--image", args.image),
                ("--icon", args.icon),
                ("--symbols", args.symbols),
                ("--heap-kib", args.heap_kib),
            )
            if value is None
        ]
        if missing:
            parser.error(f"the arguments {' '.join(missing)} are required")
        names = read_names(args.names) if args.names else None
        modules = []
        if args.module:
            if not args.elf:
                parser.error("--module needs --elf")
            image_id = build_id(run_tool([args.prefix + "readelf", "-n", args.elf]))
            image_table = run_tool([args.prefix + "readelf", "-sW", args.elf])
            image_symbols = elf_symbols(image_table)
            for module_name, elf in args.module:
                what = f"{module_name}.emod"
                data = read_module(args.prefix, elf, image_id, image_symbols, what)
                image_bytes, bss_bytes, count = MODULE_HEADER.unpack_from(data)[3:]
                print(
                    f"{module_name}.emod: {image_bytes + bss_bytes} bytes resident, "
                    f"{count} relocations"
                )
                modules.append((module_name, data))
        contents = package(
            args.kind,
            args.id,
            args.name,
            args.image.read_bytes(),
            args.icon.read_bytes(),
            args.heap_kib,
            args.files,
            args.symbols,
            args.version,
            names,
            pictures,
            packs,
            modules,
        )
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"eapp-pack: {error}", file=sys.stderr)
        return 1
    folder = args.out / args.id
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    for name, data in contents.items():
        (folder / name).parent.mkdir(parents=True, exist_ok=True)
        (folder / name).write_bytes(data)
    if args.zip:
        with zipfile.ZipFile(args.out / f"{args.id}.zip", "w") as bundle:
            for name, data in contents.items():
                write_entry(bundle, f"{ROOTS[args.kind]}/{args.id}/{name}", data)
    print(f"Packed {folder} for /{ROOTS[args.kind]}/{args.id}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
