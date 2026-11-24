#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check an extension's manifest.toml and compile the manifest rvx-pack writes
into the extension's .rvx.

manifest.toml:

    id = "hello_world"           # [a-z][a-z0-9_-]{0,31}, the file name before .rvx
    name = "Hello World"
    description = "What it does" # shown to listeners on its page: one plain sentence
    version = "0.1.0"
    author = "Who"
    license = "GPL-3.0-only"     # GPL-3.0 compatible
    api = "1.80"                 # the oldest core API major.minor the extension runs on
    stability = "beta"           # optional: alpha, beta or stable, the author's word
    category = "effects"         # where Settings > Extensions lists it: effects, media,
                                 # system, other, or library: one module, no sites or
                                 # content, nothing required, loaded while an extension
                                 # that requires it is
    note = "Adapted from X"      # optional: one line under the version, shown to listeners
    content = "Themes"           # optional: a folder under /Revola/ the extension reads, or a
                                 # list of them; [A-Z][a-z0-9]{1,15}. One owner per folder: a
                                 # second enabled extension naming it does not load
    requires = ["dsp"]           # optional: ids of the library extensions whose functions
                                 # the modules call, at most 4; the core loads them first

    exports = ["rv_dsp_biquad"]  # the functions a library offers, by C name; rvx-pack reads
                                 # them from here, they are not compiled into the manifest

    [[module]]                   # the first module is main, trigger always
    name = "main"
    trigger = "always"

    [[module]]
    name = "editor"
    trigger = "screen"           # loaded while one of its screens is on the stack
    screens = ["editor"]         # ids of the extension's screens
    preload = "optional"         # kept loaded with main: always, optional (the default,
                                 # when Optimize allows) or never (loads as the trigger
                                 # says); not for main

    [[site]]                     # a raw hook site, bound as RV_RAW_SITE(0)
    name = "coverflow_clear"     # a label for the site
    address = 0x08123456         # the word to patch, from the pinned firmware
    kind = "call"                # call, vtable or word
    expected = 0xEB001234        # the word there now; the core refuses a mismatch
    argc = 2                     # register arguments, at most 4; 0 for word

    [[asset]]
    name = "eq-icon"             # [a-z0-9_-]{1,31}
    file = "assets/eq.png"       # .png becomes a bitmap, anything else is data
    pane = "icon"                # for a menu pane: icon (a 78 x 121 coverage mask, drawn in
                                 # the system colour), picture (one of the pane sizes, which
                                 # picks the pane's shape) or badge (20 x 16, beside a count line)
    preload = "optional"         # read into RAM as stored with the main module:
                                 # always, optional (the default, when Optimize allows)
                                 # or never (the default with compress = false)

    [[asset]]
    name = "song"
    file = "song.mp3"
    compress = false             # data only; keeps it raw so asset_read can seek

Raw sites are for code no core site covers yet; first-party extensions use core
sites only.

    python3 manifest.py imports LIB/manifest.toml ...
    python3 manifest.py exports manifest.toml
    python3 manifest.py makefile manifest.toml

print linker scripts: `imports` defines every export of the named libraries as
0, so a module that calls them links, and rvx-pack turns the words that name
them into imports the core fills in at load time; `exports` keeps a library's
own exports through --gc-sections (nothing for any other extension).
`makefile` prints the id, modules, required libraries and asset files as the
Make variables rv.mk reads.

Ships in the SDK tarball with bitmap.py beside it and depends on nothing else.
"""

import re
import struct
import sys

import bitmap

MAGIC = 0x464D5652  # RVMF
VERSION = 4
STABILITY = {"alpha": 1, "beta": 2, "stable": 3}  # flags bits 0-1
# Settings > Extensions lists a category per row, in this order, libraries last.
CATEGORIES = {"effects": 1, "media": 2, "system": 3, "other": 4, "library": 5}
# Magic, version, api, flags, module and site counts and string bytes; the offsets
# of the six texts, the note, the first content folder and the first required id;
# the category; the folder and required counts.
HEADER = struct.Struct("<7I10H2I")
# Name, trigger, screen count and first screen, preload class, a reserved 0.
MODULE = struct.Struct("<HBBHBB")
SITE = struct.Struct("<IIBBH")
SITE_KINDS = {"call": 0, "vtable": 1, "word": 4}  # enum core_site_kind
MAX_SITES = 8
RUNTIME_BASE = 0x08000000
NATIVE_HEAP_BASE = 0x08B3B1B0  # OSOS 2.0.5; raw sites lie below it
TRIGGERS = {"always": 0, "screen": 1, "manual": 2}
TEXT_FIELDS = ("id", "name", "description", "version", "author", "license")
ID_PATTERN = re.compile(r"[a-z][a-z0-9_-]{0,31}")
# The core's own id, which owns its bitmap assets; no extension may take it.
CORE_ID = "revola"
RESERVED_IDS = {CORE_ID}
MODULE_PATTERN = re.compile(r"[a-z0-9_-]+")
ASSET_PATTERN = re.compile(r"[a-z0-9_-]{1,31}")
ASSET_KEYS = {"name", "file", "preload", "compress", "pane"}
PRELOAD = ("always", "optional", "never")
# What a path in a generated makefile cannot hold.
FILE_UNSAFE = re.compile(r"[\s$#:%;\\|*?\[\]()]")
CONTENT_PATTERN = re.compile(r"[A-Z][a-z0-9]{1,15}")
# The core's folders; the pattern fixes the case, so an exact match covers FAT's.
CONTENT_RESERVED = {"Extensions", "Data", "Settings", "Firmware", "Crashes"}
MAX_CONTENT = 4
MAX_REQUIRES = 4
EXPORT_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
LICENSES = {
    "GPL-3.0-only",
    "GPL-3.0-or-later",
    "GPL-2.0-or-later",
    "LGPL-2.1-or-later",
    "LGPL-3.0-only",
    "LGPL-3.0-or-later",
    "MIT",
    "BSD-2-Clause",
    "BSD-3-Clause",
    "Apache-2.0",
    "MPL-2.0",
    "0BSD",
    "Zlib",
}
MAX_MODULES = 15  # main and at most 14 more
MAX_BYTES = 1024  # CORE_MANIFEST_MAX
MANIFEST_KEYS = {
    *TEXT_FIELDS,
    "note",
    "api",
    "stability",
    "content",
    "requires",
    "category",
    "exports",
    "module",
    "site",
    "asset",
}
MODULE_KEYS = {"name", "trigger", "screens", "preload"}
SITE_KEYS = {"name", "address", "kind", "expected", "argc"}


class Strings:
    def __init__(self):
        self.data = bytearray()

    def add(self, text):
        if not isinstance(text, str) or "\0" in text:
            raise ValueError(f"not a string: {text!r}")
        offset = len(self.data)
        self.data += text.encode() + b"\0"
        if len(self.data) > 0xFFFF:
            raise ValueError("strings exceed 64 KiB")
        return offset


def fnv1a(text):
    """FNV-1a of a string, as the core hashes ids and export names."""
    value = 2166136261
    for byte in text.encode():
        value = (value ^ byte) * 16777619 & 0xFFFFFFFF
    return value


def api_word(version):
    """A quoted "major.minor"; TOML reads an unquoted 1.80 as the float 1.8."""
    match = isinstance(version, str) and re.fullmatch(r"(\d+)\.(\d+)", version)
    if not match or int(match[1]) > 0xFFFF or int(match[2]) > 0xFFFF:
        raise ValueError(f"api is major.minor, not {version!r}")
    return int(match[1]) << 16 | int(match[2])


def compile_site(site, strings):
    name = site.get("name", "")
    kind = site.get("kind", "")
    address = site.get("address")
    expected = site.get("expected")
    argc = site.get("argc", 0)
    if not MODULE_PATTERN.fullmatch(name):
        raise ValueError(f"bad site name {name!r}")
    if kind not in SITE_KINDS:
        raise ValueError(f"site {name}: kind is call, vtable or word, not {kind!r}")
    if (
        not isinstance(address, int)
        or address % 4
        or not (RUNTIME_BASE <= address < NATIVE_HEAP_BASE)
    ):
        raise ValueError(f"site {name}: address must be a word inside the OSOS image")
    if not isinstance(expected, int) or not 0 <= expected <= 0xFFFFFFFF:
        raise ValueError(f"site {name}: expected must be a 32-bit word")
    if kind == "call" and expected >> 24 != 0xEB:
        raise ValueError(f"site {name}: a call site's expected word is a BL")
    if not isinstance(argc, int) or not 0 <= argc <= 4 or (kind == "word" and argc):
        raise ValueError(f"site {name}: argc is 0 to 4, and 0 for word")
    for field in ("chain", "mode"):
        if field in site:
            raise ValueError(
                f"site {name}: raw sites are exclusive, {field} is not allowed"
            )
    if set(site) - SITE_KEYS:
        raise ValueError(f"site {name}: unknown keys {sorted(set(site) - SITE_KEYS)}")
    return SITE.pack(address, expected, SITE_KINDS[kind], argc, strings.add(name))


def compile_manifest(document):
    """The compiled manifest for a parsed manifest.toml dictionary."""
    unknown = set(document) - MANIFEST_KEYS
    if unknown:
        raise ValueError(f"unknown keys {sorted(unknown)}")
    for field in TEXT_FIELDS:
        if not isinstance(document.get(field), str):
            raise ValueError(f"missing {field}")
    if not isinstance(document.get("note", ""), str):
        raise ValueError("note is a string")
    if "api" not in document:
        raise ValueError("missing api")
    if not ID_PATTERN.fullmatch(document["id"]) or document["id"] in RESERVED_IDS:
        raise ValueError(f"bad id {document['id']!r}")
    if document["license"] not in LICENSES:
        raise ValueError(f"license {document['license']!r} is not GPL-3.0 compatible")
    modules = document.get("module", [])
    if not modules or len(modules) > MAX_MODULES:
        raise ValueError(f"1 to {MAX_MODULES} modules")
    library = is_library(document)
    exports(document)
    if library:
        if len(modules) != 1:
            raise ValueError("a library has one module")
        for key in ("site", "content", "requires"):
            if key in document:
                raise ValueError(f"a library has no {key}")
    if modules[0].get("name") != "main" or modules[0].get("trigger") != "always":
        raise ValueError("the first module is main with trigger always")
    names = set()
    strings = Strings()
    texts = [strings.add(document[field]) for field in TEXT_FIELDS]
    texts.append(strings.add(document.get("note", "")))
    content = content_folders(document)
    content_offset = strings.add(content[0]) if content else 0
    for folder in content[1:]:
        strings.add(folder)
    required = requires(document)
    requires_offset = strings.add(required[0]) if required else 0
    for identifier in required[1:]:
        strings.add(identifier)
    records = []
    for module in modules:
        name = module.get("name", "")
        trigger = module.get("trigger", "")
        screens = module.get("screens", [])
        if set(module) - MODULE_KEYS:
            raise ValueError(
                f"module {name}: unknown keys {sorted(set(module) - MODULE_KEYS)}"
            )
        if not isinstance(screens, list) or not all(
            isinstance(screen, str) and screen for screen in screens
        ):
            raise ValueError(f"module {name}: screens is a list of screen ids")
        if not MODULE_PATTERN.fullmatch(name) or name in names:
            raise ValueError(f"bad or repeated module name {name!r}")
        names.add(name)
        if trigger not in TRIGGERS:
            raise ValueError(f"module {name}: unknown trigger {trigger!r}")
        if (trigger == "screen") != bool(screens) or len(screens) > 255:
            raise ValueError(f"module {name}: screens go with the screen trigger")
        if trigger == "always" and module is not modules[0]:
            raise ValueError(f"module {name}: only main loads always")
        if trigger == "always" and "preload" in module:
            raise ValueError(f"module {name}: main carries no preload")
        preload = "always" if trigger == "always" else module.get("preload", "optional")
        if preload not in PRELOAD:
            raise ValueError(f"module {name}: preload is one of {', '.join(PRELOAD)}")
        name_offset = strings.add(name)
        screens_offset = strings.add(screens[0]) if screens else 0
        for screen in screens[1:]:
            strings.add(screen)
        records.append(
            MODULE.pack(
                name_offset,
                TRIGGERS[trigger],
                len(screens),
                screens_offset,
                PRELOAD.index(preload),
                0,
            )
        )
    sites = document.get("site", [])
    if len(sites) > MAX_SITES:
        raise ValueError(f"at most {MAX_SITES} sites")
    if len({site.get("address") for site in sites}) != len(sites):
        raise ValueError("two sites at one address")
    site_records = [compile_site(site, strings) for site in sites]
    header = HEADER.pack(
        MAGIC,
        VERSION,
        api_word(document["api"]),
        stability_bits(document),
        len(records),
        len(site_records),
        len(strings.data),
        *texts,
        content_offset,
        requires_offset,
        CATEGORIES[category(document)],
        len(content),
        len(required),
    )
    compiled = header + b"".join(site_records) + b"".join(records) + bytes(strings.data)
    if len(compiled) > MAX_BYTES:
        raise ValueError(
            f"the compiled manifest is {len(compiled)} bytes, the core reads {MAX_BYTES}"
        )
    return compiled


def content_folders(document):
    """The content folders as a list, checked; empty without a content field."""
    content = document.get("content", [])
    if isinstance(content, str):
        content = [content]
    if not isinstance(content, list) or not all(isinstance(c, str) for c in content):
        raise ValueError("content is a folder name or a list of them")
    if len(content) > MAX_CONTENT or len(set(content)) != len(content):
        raise ValueError(f"content lists at most {MAX_CONTENT} distinct folders")
    for folder in content:
        if not CONTENT_PATTERN.fullmatch(folder):
            raise ValueError(f"bad content folder {folder!r}")
        if folder in CONTENT_RESERVED:
            raise ValueError(f"content folder {folder!r} belongs to the core")
    return content


def requires(document):
    """The ids of the libraries the extension requires, checked; empty without
    a requires field."""
    required = document.get("requires", [])
    if not isinstance(required, list) or not all(isinstance(r, str) for r in required):
        raise ValueError("requires is a list of extension ids")
    if len(required) > MAX_REQUIRES or len(set(required)) != len(required):
        raise ValueError(f"requires lists at most {MAX_REQUIRES} distinct ids")
    for identifier in required:
        if not ID_PATTERN.fullmatch(identifier) or identifier in RESERVED_IDS:
            raise ValueError(f"bad required id {identifier!r}")
        if identifier == document.get("id"):
            raise ValueError("an extension cannot require itself")
    return required


def category(document):
    """The manifest's category, checked."""
    name = document.get("category")
    if not isinstance(name, str) or name not in CATEGORIES:
        raise ValueError(f"category is one of {', '.join(CATEGORIES)}")
    return name


def is_library(document):
    """Whether the manifest describes a library."""
    return category(document) == "library"


def exports(document):
    """The functions a library exports, checked: C names with distinct hashes.
    Empty, and required to be absent, for any other extension."""
    names = document.get("exports", [])
    if not is_library(document):
        if "exports" in document:
            raise ValueError("only a library has exports")
        return []
    if not isinstance(names, list) or not all(isinstance(n, str) for n in names):
        raise ValueError("exports is a list of function names")
    if not names:
        raise ValueError("a library exports at least one function")
    if len(set(names)) != len(names):
        raise ValueError("repeated export")
    if len({fnv1a(n) for n in names}) != len(names):
        raise ValueError("two exports have the same hash")
    for name in names:
        if not EXPORT_PATTERN.fullmatch(name):
            raise ValueError(f"bad export name {name!r}")
    return names


def imports_script(documents):
    """The linker script that defines every export of the library manifests as
    0, one `name = 0;` line each."""
    lines = []
    for document in documents:
        if not is_library(document):
            raise ValueError(f"{document.get('id')} is not a library")
        lines += [f"{name} = 0;" for name in exports(document)]
    return "".join(line + "\n" for line in lines)


def exports_script(document):
    """The linker script that references every export of a library, one
    `EXTERN(name)` line each, so that --gc-sections keeps them; empty for any
    other extension."""
    return "".join(f"EXTERN({name})\n" for name in exports(document))


def stability_bits(document):
    """0 without a stability field, else its value in STABILITY."""
    if "stability" not in document:
        return 0
    if document["stability"] not in STABILITY:
        raise ValueError(f"stability must be one of {', '.join(STABILITY)}")
    return STABILITY[document["stability"]]


def assets(document):
    """The manifest's assets in declaration order, checked, with defaults filled in."""
    checked = []
    for asset in document.get("asset", []):
        name = asset.get("name", "")
        if not isinstance(name, str) or not ASSET_PATTERN.fullmatch(name):
            raise ValueError(f"bad asset name {name!r}")
        if name in (a["name"] for a in checked):
            raise ValueError(f"repeated asset name {name!r}")
        if set(asset) - ASSET_KEYS:
            raise ValueError(
                f"asset {name}: unknown keys {sorted(set(asset) - ASSET_KEYS)}"
            )
        if not isinstance(asset.get("file"), str) or not asset["file"]:
            raise ValueError(f"asset {name}: file is a path")
        if FILE_UNSAFE.search(asset["file"]):
            raise ValueError(
                f"asset {name}: no spaces or $ # : % ; \\ | * ? [ ] ( ) in a file path"
            )
        if not isinstance(asset.get("compress", True), bool):
            raise ValueError(f"asset {name}: compress is true or false")
        png = asset["file"].lower().endswith(".png")
        if png and "compress" in asset:
            raise ValueError(f"asset {name}: bitmaps are always compressed")
        pane = asset.get("pane")
        if pane is not None and pane not in bitmap.PANES:
            raise ValueError(f"asset {name}: pane is one of {', '.join(bitmap.PANES)}")
        if pane and not png:
            raise ValueError(f"asset {name}: a pane {pane} is a .png")
        compress = asset.get("compress", True)
        preload = asset.get("preload", "optional" if compress else "never")
        if preload not in PRELOAD:
            raise ValueError(f"asset {name}: preload is one of {', '.join(PRELOAD)}")
        if preload != "never" and not compress:
            raise ValueError(f"asset {name}: an uncompressed asset is never preloaded")
        checked.append(
            {
                "name": name,
                "file": asset["file"],
                "preload": preload,
                "compress": compress,
                "pane": pane,
            }
        )
    return checked


def makefile(document):
    """The Make variables rv.mk takes from a checked manifest: the id, the
    modules in their order, the required libraries and the asset files."""
    compile_manifest(document)
    return (
        f"MANIFEST_ID := {document['id']}\n"
        f"MANIFEST_MODULES := {' '.join(m['name'] for m in document['module'])}\n"
        f"MANIFEST_REQUIRES := {' '.join(requires(document))}\n"
        f"MANIFEST_ASSETS := {' '.join(a['file'] for a in assets(document))}\n"
    )


def main():
    import argparse
    from pathlib import Path

    import tomllib

    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    imports = commands.add_parser(
        "imports", help="print a linker script defining the libraries' exports as 0"
    )
    imports.add_argument("manifests", type=Path, nargs="+", metavar="manifest.toml")
    exports_ = commands.add_parser(
        "exports", help="print a linker script that keeps a library's exports"
    )
    exports_.add_argument("manifest", type=Path, metavar="manifest.toml")
    make = commands.add_parser("makefile", help="print the Make variables rv.mk reads")
    make.add_argument("manifest", type=Path, metavar="manifest.toml")
    args = parser.parse_args()
    try:
        if args.command == "imports":
            documents = [tomllib.loads(path.read_text()) for path in args.manifests]
            sys.stdout.write(imports_script(documents))
        elif args.command == "exports":
            sys.stdout.write(exports_script(tomllib.loads(args.manifest.read_text())))
        else:
            sys.stdout.write(makefile(tomllib.loads(args.manifest.read_text())))
    except ValueError as error:
        sys.exit(f"manifest: {error}")


if __name__ == "__main__":
    sys.exit(main())
