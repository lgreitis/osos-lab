"""Semantic language catalogs and offline translation compilation."""

import hashlib
import json
import xml.etree.ElementTree as ET

from .language_format import encode, language_tag
from .ui.parser import format_signature

NATIVE_TAGS = (
    "en",
    "ja",
    "cs",
    "da",
    "de",
    "es",
    "fr",
    "el",
    "it",
    "hu",
    "nl",
    "nb",
    "pt",
    "ru",
    "pl",
    "fi",
    "sv",
    "tr",
    "ko",
    "zh-Hans",
    "zh-Hant",
)


def fingerprint(catalog):
    content = {key: value for key, value in catalog.items() if key != "sha256"}
    return hashlib.sha256(
        json.dumps(
            content, ensure_ascii=False, sort_keys=True, separators=(",", ":")
        ).encode()
    ).hexdigest()


def export_catalog(resources, firmware):
    entries = []
    for resource_id, text in sorted(resources.strings.items()):
        entries.append(
            {
                "id": resource_id,
                "english": text.default,
                "parts": [{"key": key, "english": value} for key, value in text.recipe],
                "format": resources.formats.get(resource_id),
                "writable": True,
            }
        )
    catalog = {"schema": 1, "firmware": firmware, "entries": entries}
    catalog["sha256"] = fingerprint(catalog)
    return catalog


def add_native(catalog, firmware, bank_index):
    validate_catalog(catalog)
    if catalog["firmware"] != {
        "bytes": len(firmware),
        "sha256": hashlib.sha256(firmware).hexdigest(),
    }:
        raise ValueError("Native firmware fingerprint mismatch")
    entries = list(catalog["entries"])
    for (kind, resource_id), (offset, size) in sorted(bank_index.items()):
        if kind != "Str ":
            continue
        if offset < 0 or size < 1 or offset + size > len(firmware):
            raise ValueError(f"Invalid native string bounds {resource_id:#x}")
        data = firmware[offset : offset + size]
        if len(data) != size or not data.endswith(b"\0") or b"\0" in data[:-1]:
            raise ValueError(f"Invalid native string {resource_id:#x}")
        text = data[:-1].decode("utf-8")
        entries.append(
            {
                "id": resource_id,
                "english": text,
                "parts": [{"key": f"apple:{resource_id:08x}", "english": text}],
                "format": None,
                "writable": "%" not in text,
            }
        )
    result = {**catalog, "entries": sorted(entries, key=lambda entry: entry["id"])}
    result["sha256"] = fingerprint(result)
    validate_catalog(result)
    return result


def validate_catalog(catalog):
    if catalog.get("schema") != 1 or catalog.get("sha256") != fingerprint(catalog):
        raise ValueError("Invalid language catalog fingerprint or schema")
    firmware = catalog["firmware"]
    if len(bytes.fromhex(firmware["sha256"])) != 32 or firmware["bytes"] <= 0:
        raise ValueError("Invalid native firmware fingerprint")
    previous, keys = 0, {}
    for entry in catalog["entries"]:
        resource_id = entry["id"]
        if not previous < resource_id <= 0xFFFFFFFF:
            raise ValueError("Invalid catalog resource order")
        previous = resource_id
        if "".join(part["english"] for part in entry["parts"]) != entry["english"]:
            raise ValueError("Catalog composition does not match English text")
        if "\0" in entry["english"]:
            raise ValueError("Embedded NUL in catalog")
        if entry["format"] is not None:
            if format_signature(entry["english"]) != entry["format"]:
                raise ValueError("Invalid catalog format signature")
        for part in entry["parts"]:
            key = part["key"]
            if key:
                if key in keys and keys[key] != part["english"]:
                    raise ValueError(f"Conflicting catalog key: {key}")
                keys[key] = part["english"]
    return keys


def read_source(path):
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as error:
        raise ValueError(f"Invalid language XML: {path}") from error
    if root.tag != "language" or set(root.attrib) != {"tag", "name", "english-name"}:
        raise ValueError("Expected language tag, name and english-name")
    tag = language_tag(root.attrib["tag"])
    if tag.casefold() == "en":
        raise ValueError("English is built in; no external English pack is required")
    if not root.attrib["name"] or not root.attrib["english-name"]:
        raise ValueError("Language names must not be empty")
    if (root.text or "").strip():
        raise ValueError("Unexpected text in language source")
    translations = {}
    for node in root:
        if (
            node.tag != "string"
            or set(node.attrib) != {"key", "text"}
            or list(node)
            or (node.text or "").strip()
            or (node.tail or "").strip()
        ):
            raise ValueError("Expected string key and text")
        key, value = node.attrib["key"], node.attrib["text"]
        if not key or key in translations or "\0" in value:
            raise ValueError(f"Invalid or duplicate language key: {key}")
        translations[key] = value
    return {
        "tag": tag,
        "name": root.attrib["name"],
        "english_name": root.attrib["english-name"],
        "native_index": NATIVE_TAGS.index(tag) if tag in NATIVE_TAGS else -1,
    }, translations


def compile_pack(catalog, metadata, translations):
    keys = validate_catalog(catalog)
    unknown = translations.keys() - keys.keys()
    if unknown:
        raise ValueError(f"Unknown language keys: {', '.join(sorted(unknown))}")
    strings = {}
    for entry in catalog["entries"]:
        if not any(part["key"] in translations for part in entry["parts"]):
            continue
        text = "".join(
            translations.get(part["key"], part["english"]) for part in entry["parts"]
        )
        if not entry["writable"] and text != entry["english"]:
            raise ValueError(f"Native format is not supported: {entry['id']:#x}")
        if entry["format"] is not None and format_signature(text) != entry["format"]:
            raise ValueError(f"Mismatched language format arguments: {entry['id']:#x}")
        strings[entry["id"]] = text
    return encode(
        metadata["tag"],
        bytes.fromhex(catalog["firmware"]["sha256"]),
        bytes.fromhex(catalog["sha256"]),
        strings,
    )
