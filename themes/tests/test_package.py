# SPDX-License-Identifier: GPL-3.0-only
"""RTM4/RTH4 theme folder compiler, inspectors and source manifest contracts."""

import contextlib
import io
import json
import os
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from patching.tests.test_bitmap import png
from patching.theme_format import HEADER, RECORD, TINT_BYTES, catalog_hash, digest
from themes import package
from themes.compiler import describe
from themes.package import (
    METADATA,
    compile_package,
    inspect_appearance,
    inspect_metadata,
    max_body,
    validate_package,
)
from themes.style import COLOR_DEFAULTS, COLOR_ROLES, COLORS, OPTIONS_BYTES, STYLE_BYTES

ROOT = Path(__file__).resolve().parents[2]


def indexed():
    pixels = bytes([0, 1, 0, 1])
    body = struct.pack("<I", 2) + bytes([3, 2, 1, 255, 6, 5, 4, 127]) + pixels
    return struct.pack("<4H5I", 0x64, 0, 4, 8, 0, 0, 1, 4, len(body)) + body


def argb(width=3):
    pixels = bytes(4 * width)
    return (
        struct.pack("<4H5I", 0x1888, 0, 4 * width, 32, 0, 0, 1, width, len(pixels))
        + pixels
    )


def mask(fmt=8):
    pixels = bytes(4) if fmt == 8 else bytes(2)
    return (
        struct.pack("<4H5I", fmt, 0, len(pixels), fmt, 0, 0, 1, 4, len(pixels)) + pixels
    )


def rgb565():
    pixels = bytes(8)
    return struct.pack("<4H5I", 0x565, 0, 8, 16, 0, 0, 1, 4, 8) + pixels


def catalog(duplicate_name=False):
    bitmaps = [
        dict(describe(1, 0, indexed(), "Alpha"), id=0x10),
        dict(describe(2, 0, argb(), "Beta"), id=0x20),
        dict(describe(3, 0, argb(1), "Beta" if duplicate_name else "Gamma"), id=0x30),
        dict(describe(4, 0, mask(), "Delta"), id=0x40),
        dict(describe(5, 0, mask(4), "Epsilon"), id=0x50),
        dict(describe(6, 0, rgb565(), "Zeta"), id=0x60),
    ]
    return {
        "schema": 1,
        "input": {"bytes": 1, "sha256": digest(b"x")},
        "bitmaps": bitmaps,
    }


def manifest(**appearances):
    return {
        "schema": 1,
        "id": "test",
        "name": "Test",
        "appearances": appearances or {"light": {}},
    }


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name) / "src"
        self.root.mkdir()
        self.catalog = catalog()
        rgba = bytes([10, 20, 30, 50, 40, 50, 60, 70]) * 2
        (self.root / "alpha.png").write_bytes(png(4, 1, 6, b"\0" + rgba))
        (self.root / "beta.png").write_bytes(png(3, 1, 6, b"\0" + bytes(range(12))))
        # Coverage art: alpha ramp over an arbitrary color, and an opaque gray ramp.
        alpha = b"".join(bytes((200, 10, 10, a)) for a in (0, 85, 170, 255))
        (self.root / "alpha-ramp.png").write_bytes(png(4, 1, 6, b"\0" + alpha))
        gray = b"".join(bytes((v, v, v, 255)) for v in (0, 86, 170, 255))
        (self.root / "gray-ramp.png").write_bytes(png(4, 1, 6, b"\0" + gray))
        three = b"".join(bytes((200, 10, 10, a)) for a in (0, 128, 255))
        (self.root / "alpha-ramp3.png").write_bytes(png(3, 1, 6, b"\0" + three))
        (self.root / "color.png").write_bytes(
            png(4, 1, 6, b"\0" + bytes([200, 10, 10, 255]) * 4)
        )

    def tearDown(self):
        self.directory.cleanup()

    def compile(self, source, catalog=None):
        path = self.root / "theme.json"
        text = source if isinstance(source, str) else json.dumps(source)
        path.write_text(text)
        return compile_package(catalog or self.catalog, path)

    def rejects(self, source, pattern, catalog=None):
        with self.assertRaisesRegex(ValueError, pattern):
            self.compile(source, catalog)

    def test_defaults_preserve_classic_colors(self):
        files = self.compile(manifest(light={}, dark={}))
        self.assertEqual(
            sorted(files), ["dark.bin", "light.bin", "report.json", "theme.bin"]
        )
        light = bytes.fromhex(
            "ffffffff202020ff236cc5ff3185d0ffffffffff236cc5ff7e8183ff"
        )
        dark = bytes.fromhex("181818ffe8e8e8ff00a1ffff00a1ffffffffffff00a1ffff383838ff")
        self.assertEqual(files["light.bin"][HEADER.size : HEADER.size + COLORS], light)
        self.assertEqual(files["dark.bin"][HEADER.size : HEADER.size + COLORS], dark)
        self.assertEqual(len(COLOR_ROLES), 7)
        self.assertEqual(COLOR_ROLES[0], "background")
        self.assertEqual(COLOR_ROLES[-1], "cover_border")

    def test_headers_and_metadata_wire_layout(self):
        self.assertEqual((METADATA.size, HEADER.size, COLORS), (120, 52, 28))
        self.assertEqual((OPTIONS_BYTES, STYLE_BYTES), (828, 856))
        files = self.compile(manifest(dark={"colors": {"accent_fill": "#ABCDEF"}}))
        self.assertEqual(sorted(files), ["dark.bin", "report.json", "theme.bin"])
        magic, fingerprint, mask, raw_id, raw_name = METADATA.unpack(files["theme.bin"])
        self.assertEqual(
            (magic, fingerprint, mask), (b"RTM4", catalog_hash(self.catalog), 2)
        )
        self.assertEqual(
            (raw_id.rstrip(b"\0"), raw_name.rstrip(b"\0")), (b"test", b"Test")
        )
        pack = files["dark.bin"]
        magic, fingerprint, code, size, count, crc = HEADER.unpack_from(pack)
        self.assertEqual((magic, code, size, count), (b"RTH4", 1, STYLE_BYTES, 0))
        self.assertEqual(crc, zlib.crc32(pack[HEADER.size :]))
        self.assertEqual(
            pack[HEADER.size + 8 : HEADER.size + 12], bytes.fromhex("abcdefff")
        )
        info = inspect_metadata(files["theme.bin"], self.catalog)
        self.assertEqual(info["appearances"], ["dark"])

    def test_bitmaps_are_sorted_same_size_native_records(self):
        files = self.compile(
            manifest(
                light={
                    "bitmaps": [
                        {"name": "Beta", "file": "beta.png"},
                        {"id": "0x10", "type": "BMap", "file": "alpha.png"},
                    ]
                }
            )
        )
        pack = files["light.bin"]
        self.assertEqual(HEADER.unpack_from(pack)[4], 2)
        info = inspect_appearance(pack, self.catalog)
        assets = self.catalog["bitmaps"]
        self.assertEqual([r["index"] for r in info["records"]], [0, 1])
        for record in info["records"]:
            asset = assets[record["index"]]
            self.assertEqual(record["offset"], asset["data_offset"])
            self.assertEqual(record["bytes"], asset["bytes"] - asset["data_offset"])
        body = pack[HEADER.size :]
        self.assertEqual(RECORD.unpack_from(body, STYLE_BYTES), (0, 32, 12))
        self.assertEqual(
            body[STYLE_BYTES + 12 : STYLE_BYTES + 24],
            bytes([30, 20, 10, 50, 60, 50, 40, 70, 0, 1, 0, 1]),
        )
        self.assertEqual(len(body) % 4, 0)
        self.assertLessEqual(len(body), max_body(self.catalog))
        report = json.loads(files["report.json"])
        self.assertIn("not measured", report["memory_estimate"]["note"])
        self.assertIn("fonts", report["capabilities"]["unsupported"])

    def test_output_is_deterministic(self):
        source = manifest(light={"bitmaps": [{"name": "Alpha", "file": "alpha.png"}]})
        self.assertEqual(self.compile(source), self.compile(source))

    def test_rejects_unsupported_capabilities_and_unknown_fields(self):
        self.rejects(
            dict(manifest(), fonts={}), "fonts: unsupported capability: custom fonts"
        )
        self.rejects(
            manifest(dark={"layouts": {}}), r"appearances\.dark\.layouts: unsupported"
        )
        self.rejects(dict(manifest(), extra=1), "extra: unknown field")
        self.rejects(manifest(sepia={}), r"appearances\.sepia: unknown field")
        self.rejects(manifest(light={"colours": {}}), "colours: unknown field")
        self.rejects(
            manifest(
                light={"bitmaps": [{"name": "Alpha", "file": "alpha.png", "x": 1}]}
            ),
            r"bitmaps\[0\]\.x: unknown field",
        )
        self.rejects('{"schema": 1, "schema": 1}', "duplicate JSON key")
        self.rejects(dict(manifest(), schema=2), "unsupported schema 2")
        for schema in (True, 1.0, "1", None):
            self.rejects(dict(manifest(), schema=schema), "unsupported schema")
        self.rejects(dict(manifest(), appearances={}), "declare light, dark or both")

    def test_extended_palettes_and_rendering(self):
        source = manifest(
            dark={
                "colors": {"icon_tint": "#ff4477"},
                "accents": {"teal": {"selection": "#006666", "on_accent": "#ffffff"}},
                "rendering": {"selection": "flat"},
            }
        )
        files = self.compile(source)
        self.assertEqual(files["theme.bin"][:4], b"RTM4")
        self.assertEqual(files["dark.bin"][:4], b"RTH4")
        decoded = inspect_appearance(files["dark.bin"], self.catalog)
        self.assertEqual(decoded["flags"], 1)
        self.assertEqual(decoded["icon_tint"], "#ff4477")
        self.assertEqual(decoded["accents"]["teal"]["selection"], "#006666")
        self.assertEqual(len(decoded["accents"]), 9)
        self.assertEqual(len(files["dark.bin"]), HEADER.size + COLORS + OPTIONS_BYTES)
        report = json.loads(files["report.json"])["appearances"]["dark"]
        self.assertNotIn("teal", report["fallback_accents"])
        self.assertIn("yellow", report["fallback_accents"])
        self.assertTrue(report["contrast"])
        self.rejects(manifest(dark={"accents": {"cyan": {}}}), "unknown field")
        self.rejects(
            manifest(dark={"rendering": {"selection": "rounded"}}), "native or flat"
        )
        self.rejects(
            manifest(dark={"rendering": {"preview_icons": "accent"}}), "unknown field"
        )
        data = bytearray(files["dark.bin"])
        data[HEADER.size + COLORS] = 128
        struct.pack_into("<I", data, 48, zlib.crc32(data[HEADER.size :]))
        with self.assertRaisesRegex(ValueError, "rendering flags"):
            inspect_appearance(data, self.catalog)

    def test_tinted_bitmaps(self):
        files = self.compile(
            manifest(
                dark={
                    "colors": {"icon_tint": "#112233"},
                    "bitmaps": [
                        {
                            "name": "Alpha",
                            "file": "alpha-ramp.png",
                            "tint": "icon_tint",
                        },
                        {"name": "Delta", "file": "gray-ramp.png", "tint": "icon_tint"},
                        {"name": "Epsilon", "file": "alpha-ramp.png", "tint": "text"},
                        {
                            "name": "Beta",
                            "file": "alpha-ramp3.png",
                            "tint": "selection",
                        },
                    ],
                }
            )
        )
        decoded = inspect_appearance(files["dark.bin"], self.catalog)
        self.assertEqual(
            decoded["tinted"],
            {0: "icon_tint", 1: "selection", 3: "icon_tint", 4: "text"},
        )
        self.assertEqual([r["index"] for r in decoded["records"]], [0, 1, 3, 4])
        body = files["dark.bin"][HEADER.size :]
        at = STYLE_BYTES + RECORD.size
        # Paletted: two entries (transparent reserved, tint + mean coverage 170).
        self.assertEqual(body[at : at + 8], bytes([0, 0, 0, 0, 0x33, 0x22, 0x11, 170]))
        at += 12 + RECORD.size
        # ARGB: BGRA placeholder pixels whose alpha carries the coverage.
        self.assertEqual(
            body[at : at + 12],
            bytes([0, 0, 0, 0, 0x33, 0x22, 0x11, 128, 0x33, 0x22, 0x11, 255]),
        )
        at += 12 + RECORD.size
        self.assertEqual(
            body[at : at + 4], bytes([0, 86, 170, 255])
        )  # gray8 = coverage
        at += 4 + RECORD.size
        self.assertEqual(body[at : at + 2], bytes([0x05, 0xAF]))  # gray4 steps of 17
        report = json.loads(files["report.json"])["appearances"]["dark"]["bitmaps"]
        self.assertEqual(
            [item["method"] for item in report],
            [
                "tinted palette (icon_tint), reduced to 2 colors",
                "tinted argb (selection), native pixels",
                "tinted mask (icon_tint), native pixels",
                "tinted mask (text), native pixels",
            ],
        )
        # Tint-only masks set the role without a record.
        files = self.compile(
            manifest(light={"bitmaps": [{"name": "Delta", "tint": "icon_tint"}]})
        )
        decoded = inspect_appearance(files["light.bin"], self.catalog)
        self.assertEqual(
            (decoded["tinted"], decoded["records"]), ({3: "icon_tint"}, [])
        )
        report = json.loads(files["report.json"])["appearances"]["light"]["bitmaps"]
        self.assertEqual(report[0]["method"], "tinted stock mask (icon_tint)")

        def bitmap(**entry):
            return manifest(light={"bitmaps": [entry]})

        tint = "icon_tint"
        self.rejects(bitmap(name="Alpha", tint=tint), "paletted bitmaps are tinted")
        self.rejects(bitmap(name="Beta", tint=tint), "ARGB bitmaps are tinted")
        self.rejects(bitmap(name="Zeta", tint=tint), "cannot be tinted")
        self.rejects(
            bitmap(name="Zeta", file="alpha-ramp.png", tint=tint), "cannot be tinted"
        )
        self.rejects(bitmap(name="Delta", tint=True), "expected one of")
        self.rejects(bitmap(name="Delta", tint=False), "expected one of")
        self.rejects(bitmap(name="Delta", tint="well"), "expected one of")
        self.rejects(bitmap(name="Delta", file="beta.png", tint=tint), "expected 4x1")
        self.rejects(
            bitmap(name="Delta", file="color.png", tint=tint),
            "grayscale or carry alpha coverage",
        )
        self.rejects(
            manifest(
                light={
                    "bitmaps": [
                        {"name": "Delta", "tint": tint},
                        {"name": "Delta", "tint": tint},
                    ]
                }
            ),
            "duplicate replacement",
        )
        self.rejects(
            manifest(
                light={
                    "bitmaps": [
                        {"name": "Delta", "tint": tint},
                        {"name": "Delta", "file": "gray-ramp.png"},
                    ]
                }
            ),
            "duplicate replacement",
        )

    def test_inspector_checks_tint_bytes_against_records(self):
        files = self.compile(
            manifest(light={"bitmaps": [{"name": "Delta", "file": "gray-ramp.png"}]})
        )
        pack = files["light.bin"]
        body = bytearray(pack[HEADER.size :])
        tinted = COLORS + OPTIONS_BYTES - TINT_BYTES

        def tint(index, message, role=1):
            data = bytearray(body)
            data[tinted + index] = role
            sealed = bytearray(pack[: HEADER.size] + data)
            struct.pack_into("<I", sealed, 48, zlib.crc32(data))
            with self.assertRaisesRegex(ValueError, message):
                inspect_appearance(sealed, self.catalog)
            return sealed

        tint(0, "no record to rewrite")
        tint(1, "no record to rewrite")
        tint(5, "no palette or coverage")
        tint(6, "outside the catalog")
        tint(3, "unknown RTH4 tint role 9", role=9)
        legacy = pack[: HEADER.size] + body[:-TINT_BYTES]
        with self.assertRaisesRegex(ValueError, "truncated|options length"):
            inspect_appearance(legacy, self.catalog)

    def test_rejects_invalid_metadata(self):
        for identifier in ("", "Test", "-a", "a" * 32, "a b", "a\n", "é"):
            self.rejects(dict(manifest(), id=identifier), "^id:")
        for identifier in ("default", "dark", "classic"):
            self.rejects(dict(manifest(), id=identifier), "reserved")
        for identifier in ("default", "dark", "classic"):
            data = package.encode_metadata(self.catalog, identifier, "Reserved", 1)
            with self.assertRaisesRegex(ValueError, "reserved"):
                inspect_metadata(data, self.catalog)
        self.compile(dict(manifest(), id="a" * 31))
        self.rejects(dict(manifest(), name="x" * 48), "exceeds 47")
        self.rejects(dict(manifest(), name="é" * 24), "exceeds 47")
        self.compile(dict(manifest(), name="é" * 23 + "x"))
        self.rejects(dict(manifest(), name="a\0b"), "control characters")
        self.rejects(dict(manifest(), author=""), "author: expected")
        source = dict(manifest(), author="Someone", version="2")
        files = self.compile(source)
        self.assertNotIn(b"Someone", files["theme.bin"])
        self.assertEqual(
            json.loads(files["report.json"])["source"]["author"], "Someone"
        )

    def test_rejects_invalid_colors(self):
        self.rejects(
            manifest(light={"colors": {"accent": "#000000"}}), "unknown color role"
        )
        for value in ("#fff", "000000", "#00000000", "#gggggg", 0):
            self.rejects(
                manifest(light={"colors": {"text": value}}), "expected #RRGGBB"
            )
        self.rejects(manifest(light={"colors": []}), "expected an object")

    def test_rejects_invalid_bitmap_references(self):
        def bitmap(**entry):
            return manifest(light={"bitmaps": [entry]})

        self.rejects(bitmap(file="alpha.png"), "expected id or name")
        self.rejects(bitmap(name="Alpha"), r"file: required$")
        self.rejects(bitmap(name="Nope", file="alpha.png"), "not a catalog bitmap name")
        self.rejects(bitmap(id="0x99", file="alpha.png"), "not a catalog bitmap")
        self.rejects(bitmap(id="ten", file="alpha.png"), "expected a hex string")
        self.rejects(bitmap(id=True, file="alpha.png"), "expected a hex string")
        self.rejects(
            bitmap(id="0x20", name="Alpha", file="alpha.png"), "different bitmaps"
        )
        self.rejects(bitmap(name="Alpha", type="Font", file="alpha.png"), "only BMap")
        self.rejects(bitmap(name="Alpha", file="beta.png"), "expected 4x1")
        self.rejects(
            bitmap(name="Beta", file="beta.png"),
            "ambiguous",
            catalog(duplicate_name=True),
        )
        self.rejects(
            manifest(
                light={
                    "bitmaps": [
                        {"name": "Alpha", "file": "alpha.png"},
                        {"id": "0x10", "file": "alpha.png"},
                    ]
                }
            ),
            "duplicate replacement for Alpha",
        )

    def test_rejects_paths_outside_the_manifest_directory(self):
        outside = self.root.parent / "outside.png"
        outside.write_bytes((self.root / "alpha.png").read_bytes())
        (self.root / "link.png").symlink_to(outside)
        (self.root / "folder").mkdir()
        for file in (
            "../outside.png",
            str(outside),
            "a/../../outside.png",
            "link.png",
            "folder",
            "missing.png",
            "dir\\alpha.png",
            ".hidden.png",
            "",
            "a" * 256,
        ):
            self.rejects(
                manifest(light={"bitmaps": [{"name": "Alpha", "file": file}]}),
                r"bitmaps\[0\]\.file",
            )

    def test_inspectors_reject_malformed_and_truncated_packs(self):
        files = self.compile(
            manifest(
                light={"bitmaps": [{"name": "Alpha", "file": "alpha.png"}]}, dark={}
            )
        )
        validate_package(files, self.catalog)
        pack = files["light.bin"]

        def reseal(body, count=1):
            return (
                HEADER.pack(
                    b"RTH4",
                    catalog_hash(self.catalog),
                    0,
                    len(body),
                    count,
                    zlib.crc32(body),
                )
                + body
            )

        body = pack[HEADER.size :]
        cases = {
            "truncated": pack[:-4],
            "header only": pack[: HEADER.size],
            "bad magic": b"\0\0\0\0" + pack[4:],
            "catalog hash": pack[:4] + bytes(32) + pack[36:],
            "appearance": pack[:36] + struct.pack("<I", 2) + pack[40:],
            "CRC32": pack[:-1] + bytes([pack[-1] ^ 1]),
            "not opaque": reseal(body[:3] + b"\0" + body[4:]),
            "palette index": reseal(body[:-1] + b"\xff"),
            "truncated record header": reseal(body[: STYLE_BYTES + 4]),
            "trailing bytes": reseal(body + bytes(4)),
            "truncated record payload": reseal(body, count=2),
            "outside the catalog": reseal(
                body[:STYLE_BYTES] + RECORD.pack(9, 32, 12) + body[STYLE_BYTES + 12 :]
            ),
            "native bitmap": reseal(
                body[:STYLE_BYTES] + RECORD.pack(0, 28, 12) + body[STYLE_BYTES + 12 :]
            ),
        }
        for name, data in cases.items():
            with self.subTest(name):
                with self.assertRaises(ValueError):
                    inspect_appearance(data, self.catalog)
        twice = body[STYLE_BYTES:]
        with self.assertRaisesRegex(ValueError, "sorted by unique"):
            inspect_appearance(reseal(body + twice, count=2), self.catalog)
        padded = reseal(
            body[:STYLE_BYTES] + RECORD.pack(0, 32, 10) + body[STYLE_BYTES + 12 :]
        )
        with self.assertRaisesRegex(ValueError, "padding must be zero"):
            inspect_appearance(padded)

        metadata = files["theme.bin"]
        for name, data in {
            "size": metadata[:-1],
            "magic": b"RTM3" + metadata[4:],
            "hash": metadata[:4] + bytes(32) + metadata[36:],
            "mask": metadata[:36] + struct.pack("<I", 4) + metadata[40:],
            "id NUL": metadata[:40] + b"a" * 32 + metadata[72:],
            "id bytes": metadata[:40] + b"Bad\0" + bytes(28) + metadata[72:],
            "name NUL": metadata[:72] + b"n" * 48,
            "name tail": metadata[:72] + b"n\0x" + bytes(45),
            "name UTF-8": metadata[:72] + b"\xff\0" + bytes(46),
        }.items():
            with self.subTest(name):
                with self.assertRaises(ValueError):
                    inspect_metadata(data, self.catalog)

        with self.assertRaisesRegex(ValueError, "folder has"):
            validate_package(
                {k: v for k, v in files.items() if k != "dark.bin"}, self.catalog
            )
        with self.assertRaisesRegex(ValueError, "declares the light"):
            validate_package(
                dict(files, **{"dark.bin": files["light.bin"]}), self.catalog
            )
        with self.assertRaisesRegex(ValueError, "missing theme.bin"):
            validate_package({"light.bin": pack}, self.catalog)

    def test_body_limit_is_derived_from_catalog(self):
        expected = (
            STYLE_BYTES + (12 + 12) + (12 + 12) + (12 + 4) + (12 + 4) * 2 + (12 + 8)
        )
        self.assertEqual(max_body(self.catalog), expected)
        files = self.compile(
            manifest(
                light={
                    "bitmaps": [
                        {"name": "Alpha", "file": "alpha.png"},
                        {"name": "Beta", "file": "beta.png"},
                    ]
                }
            )
        )
        self.assertLessEqual(HEADER.unpack_from(files["light.bin"])[3], expected)
        small = dict(self.catalog, bitmaps=self.catalog["bitmaps"][:1])
        oversized = HEADER.pack(
            b"RTH4", catalog_hash(small), 0, STYLE_BYTES + 24 + 4, 0, 0
        ) + bytes(STYLE_BYTES + 28)
        with self.assertRaisesRegex(ValueError, "catalog maximum"):
            inspect_appearance(oversized, small)

    def test_cli_compiles_everything_before_writing(self):
        out = Path(self.directory.name) / "out"
        catalog_path = Path(self.directory.name) / "catalog.json"
        catalog_path.write_text(json.dumps(self.catalog))
        source = self.root / "theme.json"
        source.write_text(json.dumps(manifest(light={})))
        args = [str(source), "--out", str(out), "--catalog", str(catalog_path)]
        with contextlib.redirect_stdout(io.StringIO()):
            package.main(args)
        self.assertEqual(
            sorted(os.listdir(out)), ["light.bin", "report.json", "theme.bin"]
        )
        before = {name: (out / name).read_bytes() for name in os.listdir(out)}
        source.write_text(json.dumps(manifest(light={}, dark={"fonts": []})))
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            package.main(args)
        self.assertEqual(
            {name: (out / name).read_bytes() for name in os.listdir(out)}, before
        )
        source.write_text(json.dumps(manifest(dark={})))
        with contextlib.redirect_stdout(io.StringIO()):
            package.main(args)
        self.assertEqual(
            sorted(os.listdir(out)), ["dark.bin", "report.json", "theme.bin"]
        )

    def test_example_compiles_against_firmware_catalog(self):
        firmware = json.loads((ROOT / "firmware/bitmaps.json").read_text())
        files = compile_package(firmware, ROOT / "themes/examples/modern/theme.json")
        result = validate_package(files, firmware)
        self.assertEqual(result["metadata"]["appearances"], ["light", "dark"])
        self.assertNotEqual(
            result["packs"]["light.bin"]["colors"], COLOR_DEFAULTS["light"]
        )


if __name__ == "__main__":
    unittest.main()
