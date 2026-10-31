"""Versioned external UTF-8 language packs shared with the payload loader."""

import re
import struct
import zlib

MAGIC = b"RLP1"
VERSION = 1
HEADER = struct.Struct("<4sI32s32s32sIII")
ENTRY = struct.Struct("<III")
MAX_BYTES = 262144
MAX_ENTRIES = 4096
MAX_TEXT_BYTES = 4096
TAG_BYTES = 32
INDEX_HEADER = struct.Struct("<4sI32s32sIII")
INDEX_ENTRY = struct.Struct("<32s96s96si")
MAX_LANGUAGES = 64


def language_tag(tag):
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*(?:-[A-Za-z0-9]+)*", tag):
        raise ValueError("Invalid language tag")
    if len(tag.encode("ascii")) >= TAG_BYTES:
        raise ValueError("Language tag is too long")
    return tag


def encode(tag, firmware_hash, catalog_hash, strings):
    tag = language_tag(tag).encode("ascii")
    if len(firmware_hash) != 32 or len(catalog_hash) != 32:
        raise ValueError("Expected SHA-256 fingerprints")
    if len(strings) > MAX_ENTRIES:
        raise ValueError("Too many language entries")
    table, text = bytearray(), bytearray()
    for resource_id, value in sorted(strings.items()):
        if not isinstance(resource_id, int) or not 0 < resource_id <= 0xFFFFFFFF:
            raise ValueError("Invalid language resource ID")
        data = value.encode("utf-8") + b"\0"
        if b"\0" in data[:-1] or len(data) > MAX_TEXT_BYTES:
            raise ValueError("Invalid language text length or embedded NUL")
        table.extend(ENTRY.pack(resource_id, len(text), len(data)))
        text.extend(data)
    body = table + text
    if HEADER.size + len(body) > MAX_BYTES:
        raise ValueError("Language pack exceeds memory budget")
    return (
        HEADER.pack(
            MAGIC,
            VERSION,
            firmware_hash,
            catalog_hash,
            tag,
            len(strings),
            len(body),
            zlib.crc32(body),
        )
        + body
    )


def decode(data, firmware_hash, catalog_hash):
    if not HEADER.size <= len(data) <= MAX_BYTES:
        raise ValueError("Invalid language pack size")
    magic, version, firmware, catalog, tag, count, size, crc = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION:
        raise ValueError("Unsupported language pack version")
    if firmware != firmware_hash or catalog != catalog_hash:
        raise ValueError("Language pack fingerprint mismatch")
    if count > MAX_ENTRIES or size != len(data) - HEADER.size:
        raise ValueError("Invalid language pack bounds")
    body = data[HEADER.size :]
    if count * ENTRY.size > size or zlib.crc32(body) != crc:
        raise ValueError("Invalid language pack checksum or index")
    tag_bytes, separator, padding = tag.partition(b"\0")
    if not separator or any(padding):
        raise ValueError("Invalid language tag padding")
    try:
        tag = language_tag(tag_bytes.decode("ascii"))
        strings = {}
        previous, position = 0, 0
        text = body[count * ENTRY.size :]
        for index in range(count):
            resource_id, offset, length = ENTRY.unpack_from(body, index * ENTRY.size)
            if resource_id <= previous or offset != position:
                raise ValueError("Invalid language entry order")
            if not 1 <= length <= MAX_TEXT_BYTES or offset + length > len(text):
                raise ValueError("Invalid language text bounds")
            value = text[offset : offset + length]
            if value[-1] != 0 or b"\0" in value[:-1]:
                raise ValueError("Invalid language text termination")
            strings[resource_id] = value[:-1].decode("utf-8")
            previous, position = resource_id, offset + length
        if position != len(text):
            raise ValueError("Unexpected trailing language data")
    except UnicodeError as error:
        raise ValueError("Invalid language text encoding") from error
    return tag, strings


def index_text(value, capacity):
    data = value.encode("utf-8")
    if not data or len(data) >= capacity or b"\0" in data:
        raise ValueError("Invalid language index text")
    return data


def encode_index(languages, firmware_hash, catalog_hash):
    if len(firmware_hash) != 32 or len(catalog_hash) != 32:
        raise ValueError("Expected SHA-256 fingerprints")
    if len(languages) > MAX_LANGUAGES:
        raise ValueError("Too many languages")
    body = bytearray()
    tags, native_indices = set(), set()
    for metadata in sorted(languages, key=lambda item: item["tag"]):
        tag = language_tag(metadata["tag"])
        native = metadata["native_index"]
        if tag.casefold() in tags or not -1 <= native < 21:
            raise ValueError("Invalid or duplicate language index entry")
        if native >= 0 and native in native_indices:
            raise ValueError("Duplicate native language mapping")
        tags.add(tag.casefold())
        native_indices.add(native)
        body.extend(
            INDEX_ENTRY.pack(
                tag.encode("ascii"),
                index_text(metadata["name"], 96),
                index_text(metadata["english_name"], 96),
                native,
            )
        )
    return (
        INDEX_HEADER.pack(
            b"RLI1",
            VERSION,
            firmware_hash,
            catalog_hash,
            len(languages),
            len(body),
            zlib.crc32(body),
        )
        + body
    )


def decode_index(data, firmware_hash, catalog_hash):
    if (
        not INDEX_HEADER.size
        <= len(data)
        <= INDEX_HEADER.size + MAX_LANGUAGES * INDEX_ENTRY.size
    ):
        raise ValueError("Invalid language index size")
    magic, version, firmware, catalog, count, size, crc = INDEX_HEADER.unpack_from(data)
    body = data[INDEX_HEADER.size :]
    if magic != b"RLI1" or version != VERSION:
        raise ValueError("Unsupported language index version")
    if firmware != firmware_hash or catalog != catalog_hash:
        raise ValueError("Language index fingerprint mismatch")
    if count > MAX_LANGUAGES or size != len(body) or size != count * INDEX_ENTRY.size:
        raise ValueError("Invalid language index bounds")
    if zlib.crc32(body) != crc:
        raise ValueError("Invalid language index checksum")
    languages = []
    try:
        for offset in range(0, len(body), INDEX_ENTRY.size):
            tag, name, english_name, native = INDEX_ENTRY.unpack_from(body, offset)
            languages.append(
                {
                    "tag": tag.split(b"\0", 1)[0].decode("ascii"),
                    "name": name.split(b"\0", 1)[0].decode("utf-8"),
                    "english_name": english_name.split(b"\0", 1)[0].decode("utf-8"),
                    "native_index": native,
                }
            )
        if encode_index(languages, firmware_hash, catalog_hash) != data:
            raise ValueError("Invalid language index order or padding")
    except UnicodeError as error:
        raise ValueError("Invalid language index encoding") from error
    return languages
