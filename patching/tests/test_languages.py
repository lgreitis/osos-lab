"""External language pack contracts."""

import hashlib
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from patching.language_format import (
    ENTRY,
    HEADER,
    INDEX_HEADER,
    decode,
    decode_index,
    encode,
    encode_index,
)
from patching.languages import (
    add_native,
    compile_pack,
    export_catalog,
    read_source,
    validate_catalog,
)
from patching.ui.compiler import DocumentCompiler
from patching.ui.model import Document, Text
from patching.ui.resources import Resources
from tools import languages as language_tools


class LanguageTests(unittest.TestCase):
    firmware = bytes(range(32))
    catalog = bytes(reversed(range(32)))

    def pack(self, strings=None):
        return encode("de-DE", self.firmware, self.catalog, strings or {100: "Text"})

    def test_round_trip_and_deterministic_order(self):
        strings = {200: "Gr\u00f6\u00dfe: %s", 100: "", 300: "Line\nTwo"}
        pack = self.pack(strings)
        self.assertEqual(decode(pack, self.firmware, self.catalog), ("de-DE", strings))
        self.assertEqual(pack, self.pack(dict(reversed(list(strings.items())))))
        self.assertEqual(ENTRY.unpack_from(pack, HEADER.size)[0], 100)
        custom = encode("x-test", self.firmware, self.catalog, {})
        self.assertEqual(decode(custom, self.firmware, self.catalog), ("x-test", {}))

    def test_index_roundtrip_and_native_deduplication(self):
        languages = [
            {
                "tag": "x-test",
                "name": "Test",
                "english_name": "Test",
                "native_index": -1,
            },
            {
                "tag": "de",
                "name": "Deutsch",
                "english_name": "German",
                "native_index": 4,
            },
        ]
        data = encode_index(languages, self.firmware, self.catalog)
        self.assertEqual(
            decode_index(data, self.firmware, self.catalog), list(reversed(languages))
        )
        for other in (dict(languages[1]), {**languages[1], "tag": "de-DE"}):
            with self.assertRaises(ValueError):
                encode_index([languages[1], other], self.firmware, self.catalog)
        damaged = bytearray(data)
        damaged[INDEX_HEADER.size + 4] = 1
        with self.assertRaises(ValueError):
            decode_index(damaged, self.firmware, self.catalog)

    def test_semantic_identity_is_independent_of_translation_set(self):
        document = Document(
            None, {}, [], [], strings={"first": "Open", "second": "Open"}
        )
        first = document.text("@first")
        second = document.text("@second")
        self.assertNotEqual(first.recipe, second.recipe)
        document.translations["first"] = {4: "Oeffnen"}
        self.assertEqual(first.recipe, document.text("@first").recipe)
        self.assertEqual(
            Text.join(first, Text(" Hz")).recipe,
            (("cfw:inline/first", "Open"), ("", " Hz")),
        )

    def catalog_fixture(self):
        resources = Resources([])
        resources.string(100, Text("Open", parts=(("cfw:test/verb", "Open"),)))
        resources.string(101, Text("Open", parts=(("cfw:test/state", "Open"),)))
        resources.string(
            102, Text.join(Text("10"), Text(" Hz", parts=(("cfw:test/unit", " Hz"),)))
        )
        resources.string(
            103, Text("Title: %s", parts=(("cfw:test/title", "Title: %s"),))
        )
        resources.formats[103] = "s"
        return export_catalog(resources, {"bytes": 1, "sha256": self.firmware.hex()})

    def test_compiler_resource_ids_and_catalog_ignore_translation_set(self):
        root = Path(__file__).resolve().parents[2]
        templates = json.loads((root / "firmware/ui.json").read_text())["resources"]
        bindings = json.loads((root / "payload/native/ui.json").read_text())["bindings"]
        catalogs = []
        for variants in ({}, {"first": {4: "Oeffnen"}, "second": {4: "Offen"}}):
            resources = Resources(templates, bindings)
            document = Document(
                None,
                {},
                [],
                [],
                strings={"first": "Open", "second": "Open"},
                translations=variants,
            )
            compiler = DocumentCompiler(resources, document)
            first = compiler.source("@first")
            self.assertEqual(first, compiler.source("@first"))
            self.assertNotEqual(first, compiler.source("@second"))
            catalogs.append(
                export_catalog(resources, {"bytes": 1, "sha256": self.firmware.hex()})
            )
        self.assertEqual(catalogs[0], catalogs[1])

    def test_native_image_mismatch_is_rejected_before_resource_parsing(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "osos.bin"
            path.write_bytes(b"not a firmware image")
            with self.assertRaisesRegex(ValueError, "fingerprint"):
                language_tools.native_catalog(self.catalog_fixture(), path)

    def test_catalog_compiles_semantic_keys_and_compositions_without_firmware(self):
        catalog = self.catalog_fixture()
        pack = compile_pack(
            catalog,
            {"tag": "x-test"},
            {
                "cfw:test/verb": "Oeffnen",
                "cfw:test/state": "Offen",
                "cfw:test/unit": " units",
            },
        )
        self.assertEqual(
            decode(pack, self.firmware, bytes.fromhex(catalog["sha256"])),
            (
                "x-test",
                {100: "Oeffnen", 101: "Offen", 102: "10 units"},
            ),
        )

    def test_catalog_rejects_bad_keys_signatures_and_fingerprints(self):
        catalog = self.catalog_fixture()
        for translations in (
            {"bad": "x"},
            {"cfw:test/title": "%u"},
            {"cfw:test/title": "%n"},
        ):
            with self.assertRaises(ValueError):
                compile_pack(catalog, {"tag": "de"}, translations)
        catalog["entries"][0]["english"] = "Changed"
        with self.assertRaisesRegex(ValueError, "fingerprint"):
            validate_catalog(catalog)

    def test_native_catalog_uses_verified_bytes_and_locks_unknown_formats(self):
        firmware = b"Menu\0Value: %d\0"
        catalog = export_catalog(
            Resources([]),
            {
                "bytes": len(firmware),
                "sha256": hashlib.sha256(firmware).hexdigest(),
            },
        )
        catalog = add_native(
            catalog, firmware, {("Str ", 1): (0, 5), ("Str ", 2): (5, 10)}
        )
        pack = compile_pack(catalog, {"tag": "x-test"}, {"apple:00000001": "Test menu"})
        self.assertEqual(
            decode(
                pack,
                hashlib.sha256(firmware).digest(),
                bytes.fromhex(catalog["sha256"]),
            )[1],
            {1: "Test menu"},
        )
        with self.assertRaisesRegex(ValueError, "not supported"):
            compile_pack(catalog, {"tag": "x-test"}, {"apple:00000002": "Wert: %s"})

    def test_tag_sources_reject_duplicates_unknown_content_and_external_english(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "strings.lo"
            source.write_text(
                '<language tag="de" name="Deutsch" english-name="German"><string key="cfw:test/verb" text="Oeffnen"/></language>'
            )
            metadata, translations = read_source(source)
            self.assertEqual(metadata["native_index"], 4)
            self.assertEqual(translations, {"cfw:test/verb": "Oeffnen"})
            for body in (
                '<language tag="en" name="English" english-name="English"/>',
                '<language tag="x-test" name="Test" english-name="Test"><string key="k" text="a"/><string key="k" text="b"/></language>',
                '<language tag="x-test" name="Test" english-name="Test"><unknown/></language>',
                '<language tag="../bad" name="Test" english-name="Test"/>',
            ):
                source.write_text(body)
                with self.subTest(body=body), self.assertRaises(ValueError):
                    read_source(source)

    def test_source_export_and_build_needs_only_saved_catalog(self):
        catalog = self.catalog_fixture()
        metadata = {
            "tag": "x-test",
            "name": "Test",
            "english_name": "Test",
            "native_index": -1,
        }
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            language_tools.source_tree(
                [(metadata, {"cfw:test/verb": "Test open"})], directory / "sources"
            )
            language = read_source(directory / "sources/x-test/strings.lo")
            self.assertEqual(
                language_tools.publish(catalog, [language], directory / "languages"), 1
            )
            package = directory / "languages/x-test/strings.bin"
            expected = package.read_bytes()
            self.assertEqual(
                decode(expected, self.firmware, bytes.fromhex(catalog["sha256"]))[1],
                {100: "Test open"},
            )
            stored = json.loads((directory / "languages/catalog.json").read_text())
            self.assertEqual(stored, catalog)
            self.assertEqual(
                language_tools.publish(stored, [language], directory / "languages"), 1
            )
            self.assertEqual(package.read_bytes(), expected)
            invalid = (metadata, {"unknown": "bad"})
            with self.assertRaises(ValueError):
                language_tools.publish(catalog, [invalid], directory / "languages")
            self.assertEqual(package.read_bytes(), expected)

    def test_rejects_mismatched_firmware_and_catalog(self):
        for firmware, catalog in (
            (bytes(32), self.catalog),
            (self.firmware, bytes(32)),
        ):
            with self.assertRaisesRegex(ValueError, "fingerprint"):
                decode(self.pack(), firmware, catalog)

    def test_rejects_truncation_and_corruption(self):
        pack = self.pack()
        for length in range(len(pack)):
            with self.subTest(length=length), self.assertRaises(ValueError):
                decode(pack[:length], self.firmware, self.catalog)
        damaged = bytearray(pack)
        damaged[-2] ^= 1
        with self.assertRaisesRegex(ValueError, "checksum"):
            decode(damaged, self.firmware, self.catalog)

    def test_rejects_invalid_source_text_and_paths(self):
        for tag in ("../de", "de/DE", "", "de_DE", "a" * 32):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                encode(tag, self.firmware, self.catalog, {})
        for strings in ({0: "x"}, {1: "a\0b"}, {1: "a" * 4096}):
            with self.assertRaises(ValueError):
                self.pack(strings)

    def test_rejects_bad_offsets_even_with_valid_checksum(self):
        pack = bytearray(self.pack())
        struct.pack_into("<I", pack, HEADER.size + 4, 1)
        struct.pack_into("<I", pack, HEADER.size - 4, zlib.crc32(pack[HEADER.size :]))
        with self.assertRaisesRegex(ValueError, "order"):
            decode(pack, self.firmware, self.catalog)

    def test_rejects_invalid_entries_even_with_valid_checksum(self):
        original = self.pack({100: "AB", 200: "CD"})
        variants = []
        for offset, value in (
            (HEADER.size, 0),
            (HEADER.size + ENTRY.size, 100),
            (HEADER.size + 8, 0xFFFFFFFF),
        ):
            pack = bytearray(original)
            struct.pack_into("<I", pack, offset, value)
            variants.append(pack)
        for value in (b"\xffB\0", b"A\0\0", b"ABC"):
            pack = bytearray(original)
            start = HEADER.size + 2 * ENTRY.size
            pack[start : start + 3] = value
            variants.append(pack)
        for pack in variants:
            struct.pack_into(
                "<I", pack, HEADER.size - 4, zlib.crc32(pack[HEADER.size :])
            )
            with self.assertRaises(ValueError):
                decode(pack, self.firmware, self.catalog)

    def test_index_rejects_truncation_mismatch_and_padding(self):
        metadata = {
            "tag": "de",
            "name": "Deutsch",
            "english_name": "German",
            "native_index": 4,
        }
        pack = encode_index([metadata], self.firmware, self.catalog)
        for length in range(len(pack)):
            with self.assertRaises(ValueError):
                decode_index(pack[:length], self.firmware, self.catalog)
        with self.assertRaisesRegex(ValueError, "fingerprint"):
            decode_index(pack, bytes(32), self.catalog)
        damaged = bytearray(pack)
        damaged[INDEX_HEADER.size + 31] = 1
        struct.pack_into(
            "<I",
            damaged,
            INDEX_HEADER.size - 4,
            zlib.crc32(damaged[INDEX_HEADER.size :]),
        )
        with self.assertRaisesRegex(ValueError, "padding"):
            decode_index(damaged, self.firmware, self.catalog)
