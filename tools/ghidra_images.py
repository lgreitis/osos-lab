# SPDX-License-Identifier: GPL-3.0-only
"""ARM ELF construction and verified firmware memory layouts."""

import hashlib
import struct

from target_profiles import verify


def sha(data):
    return hashlib.sha256(data).hexdigest()


def word(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def region(name, address, data=b"", size=None, flags=6, source=None):
    return dict(
        name=name,
        address=address,
        data=data,
        size=len(data) if size is None else size,
        flags=flags,
        source=source,
    )


def elf(regions, entry):
    """ELF32 ARM executable with explicit load segments and section names."""
    regions = sorted(regions, key=lambda r: r["address"])
    for i, r in enumerate(regions):
        if not r["size"] or len(r["data"]) > r["size"]:
            raise ValueError("Invalid ELF region size")
        if i and regions[i - 1]["address"] + regions[i - 1]["size"] > r["address"]:
            raise ValueError("Overlapping ELF regions")
    data = bytearray(52 + 32 * len(regions))
    headers, sections, segments = [], [(0,) * 10], []
    names = bytearray(b"\0")
    for r in regions:
        data.extend(bytes((-len(data)) % 4))
        offset = len(data)
        data.extend(r["data"])
        headers.append(
            (
                1,
                offset,
                r["address"],
                r["address"],
                len(r["data"]),
                r["size"],
                r["flags"],
                1,
            )
        )
        flags = 2 | (1 if r["flags"] & 2 else 0) | (4 if r["flags"] & 1 else 0)
        sections.append(
            (
                len(names),
                1 if r["data"] else 8,
                flags,
                r["address"],
                offset,
                r["size"],
                0,
                0,
                1,
                0,
            )
        )
        names.extend(r["name"].encode() + b"\0")
        if r["data"]:
            segments.append([offset, len(r["data"])])
    return finish_elf(data, headers, sections, names, entry), segments


def finish_elf(data, headers, sections, names, entry):
    name_index = len(sections)
    string_name = len(names)
    names.extend(b".shstrtab\0")
    sections.append((string_name, 3, 0, 0, len(data), len(names), 0, 0, 1, 0))
    data.extend(names)
    data.extend(bytes((-len(data)) % 4))
    section_offset = len(data)
    for s in sections:
        data.extend(struct.pack("<10I", *s))
    struct.pack_into(
        "<16sHHIIIIIHHHHHH",
        data,
        0,
        b"\x7fELF\x01\x01\x01" + bytes(9),
        2,
        40,
        1,
        entry,
        52,
        section_offset,
        0x05000000,
        52,
        32,
        len(headers),
        40,
        len(sections),
        name_index,
    )
    for i, h in enumerate(headers):
        struct.pack_into("<8I", data, 52 + 32 * i, *h)
    return bytes(data)


def osos_regions(data, profile):
    verify(data, profile["inputs"]["osos.bin"], "osos.bin")
    layout = profile["analysis"]
    if any(
        word(data, int(offset, 0)) != value
        for offset, value in layout["startup_constants"].items()
    ):
        raise ValueError("OSOS startup copy/clear constants differ")
    regions = []
    for spec in layout["osos_regions"]:
        source, size = spec["source"], spec["size"]
        contents = b"" if source is None else data[source : source + size]
        if source is not None and len(contents) != size:
            raise ValueError("OSOS region outside input")
        regions.append(region(data=contents, **spec))
    return regions


def executable_header(data):
    if data[:2] == b"MZ":
        pe = word(data, 60)
        if data[pe : pe + 4] != b"PE\0\0":
            raise ValueError("Invalid PE signature")
        count, optional_size = (
            struct.unpack_from("<H", data, pe + 6)[0],
            struct.unpack_from("<H", data, pe + 20)[0],
        )
        optional = pe + 24
        if struct.unpack_from("<H", data, optional)[0] != 0x10B:
            raise ValueError("Expected PE32")
        base, entry = word(data, optional + 28), word(data, optional + 16)
        table, adjustment = optional + optional_size, 0
        header_size = word(data, optional + 60)
        header_address = base
    elif data[:2] == b"VZ":
        count = data[4]
        adjustment = struct.unpack_from("<H", data, 6)[0] - 40
        header_address = struct.unpack_from("<Q", data, 16)[0]
        base, entry = header_address - adjustment, word(data, 8)
        table, header_size = 40, 40 + count * 40
    else:
        raise ValueError("Expected PE32 or TE executable")
    if header_size > len(data) or table + count * 40 > len(data):
        raise ValueError("Truncated executable headers")
    return base, entry, count, table, adjustment, header_address, header_size


def pe_regions(data):
    base, entry, count, table, adjustment, header_address, header_size = (
        executable_header(data)
    )
    regions = [
        region(".headers", header_address, data[:header_size], flags=4, source=0)
    ]
    mappings = [(0, header_size, header_address)]
    for i in range(count):
        s = table + i * 40
        name = data[s : s + 8].rstrip(b"\0").decode("ascii")
        virtual_size, rva, raw_size, raw = struct.unpack_from("<4I", data, s + 8)
        raw -= adjustment
        size = max(virtual_size, raw_size)
        if not size:
            continue
        if raw < 0 or raw + raw_size > len(data):
            raise ValueError("Executable section outside file")
        characteristics = word(data, s + 36)
        flags = (
            (4 if characteristics & 0x40000000 else 0)
            | (2 if characteristics & 0x80000000 else 0)
            | (1 if characteristics & 0x20000000 else 0)
        )
        if raw_size:
            regions.append(
                region(
                    name,
                    base + rva,
                    data[raw : raw + raw_size],
                    flags=flags,
                    source=raw,
                )
            )
            mappings.append((raw, raw_size, base + rva))
        if size > raw_size:
            regions.append(
                region(
                    name + ".bss",
                    base + rva + raw_size,
                    size=size - raw_size,
                    flags=flags & ~1,
                )
            )
    return regions, base + entry, mappings
