"""Parse device configuration and relocate Apple firmware modules."""

import hashlib
import struct


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sysinfo(nor):
    """Reproduce SystemConfig+2f8 using the physical DRAM size."""
    magic, size, u1, version, u2, count = struct.unpack_from("<6I", nor)
    assert (magic, u1, version, u2) == (0x53436667, 0x2000, 0x10001, 0)
    assert count == 9 and size == 24 + count * 20 and size <= len(nor)
    entries = {}
    for i in range(count):
        offset = 24 + 20 * i
        tag = (
            struct.unpack_from("<I", nor, offset)[0].to_bytes(4, "big").decode("ascii")
        )
        assert tag not in entries
        entries[tag] = nor[offset + 4 : offset + 20]
    assert set(entries) == {
        "SrNm",
        "FwId",
        "HwId",
        "HwVr",
        "Codc",
        "SwVr",
        "MLBN",
        "Mod#",
        "Regn",
    }
    result = bytearray(0x120)
    for offset, value in {
        0: 0x53797349,
        4: 4,
        0xE0: 0x04000000,
        0xE4: 0x08000000,
        0xE8: 0x40000,
        0xEC: 0x22000000,
        0xF0: 0x100000,
        0xF4: 0x24000000,
        0x118: 0x7672736E,
        0x11C: 0x01708004,
    }.items():
        struct.pack_into("<I", result, offset, value)
    result[0x88:0x8B] = b"NA\0"
    for tag, start, length, dest in [
        ("SrNm", 0, 16, 0x18),
        ("FwId", 4, 8, 0x38),
        ("HwVr", 4, 4, 0x84),
        ("Codc", 0, 4, 0x104),
        ("SwVr", 0, 16, 0x108),
        ("Mod#", 0, 16, 0x98),
    ]:
        result[dest : dest + length] = entries[tag][start : start + length]
    region = entries["Regn"]
    if struct.unpack_from("<H", region)[0] == 1:
        result[0x92:0x96] = region[4:8]
    return bytes(result)


def relocate(original, spec):
    name, base, length, count, expected_sha = spec
    if len(original) != length or hashlib.sha256(original).hexdigest() != expected_sha:
        raise ValueError("unexpected " + name + " image")

    def read_u32(offset):
        return struct.unpack_from("<I", original, offset)[0]

    if read_u32(60) != 0x80 or read_u32(0xB4) != 0 or read_u32(0xD0) != length:
        raise ValueError("unexpected PE layout")
    for i in range(struct.unpack_from("<H", original, 0x86)[0]):
        section = 0x98 + struct.unpack_from("<H", original, 0x94)[0] + i * 40
        if (
            read_u32(section + 12) != read_u32(section + 20)
            or read_u32(section + 8) != read_u32(section + 16)
            or read_u32(section + 20) + read_u32(section + 16) > length
        ):
            raise ValueError("nonidentity section layout")
    start, size = struct.unpack_from("<II", original, 0x120)
    if start + size > length:
        raise ValueError("relocations outside image")
    cursor = start
    out = bytearray(original)
    sites = []
    while cursor < start + size:
        page, block_size = struct.unpack_from("<II", original, cursor)
        if block_size < 8 or block_size % 2 or cursor + block_size > start + size:
            raise ValueError("invalid relocation block")
        for entry_offset in range(cursor + 8, cursor + block_size, 2):
            entry = struct.unpack_from("<H", original, entry_offset)[0]
            if entry >> 12 == 0:
                continue
            site = page + (entry & 0xFFF)
            if (
                entry >> 12 != 3
                or site + 4 > length
                or site in sites
                or read_u32(site) >= length
            ):
                raise ValueError("unsupported relocation")
            struct.pack_into("<I", out, site, base + read_u32(site))
            sites.append(site)
        cursor += block_size
    if len(sites) != count:
        raise ValueError("relocation count changed")
    return bytes(out), sites
