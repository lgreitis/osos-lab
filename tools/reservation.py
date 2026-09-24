"""Reserve CFW memory in the Apple loader."""

import hashlib
import struct

INPUT_SHA = "7caf3863376cf7890adc73601d24fbf11bc9ce89c7b40366fd61b45a1d6633e8"
BASE = 0x22000000
PRE = 0xE1FC
FFS = 0xE1E0
FFS_END = FFS + 0x275C
STUB = PRE + 0x40
CALLS = ((0x10596, "fff78bf9", STUB), (0x105BE, "fef7c7ff", STUB + 16))
MMU_CALL = (0x105A6, bytes.fromhex("fdf7caff"))


def sha(b):
    return hashlib.sha256(b).hexdigest()


def thumb_bl(source, target):
    """ARMv5 Thumb-1 BL, addresses without the Thumb function-pointer bit."""
    delta = target - (source + 4)
    if source & 1 or target & 1 or not -(1 << 22) <= delta < (1 << 22):
        raise ValueError("unaligned or out-of-range Thumb BL")
    return struct.pack(
        "<HH", 0xF000 | ((delta >> 12) & 0x7FF), 0xF800 | ((delta >> 1) & 0x7FF)
    )


def verify_ffs(body):
    header = bytearray(body[FFS : FFS + 24])
    if len(header) != 24 or int.from_bytes(header[20:23], "little") != 0x275C:
        raise ValueError("unexpected PreEfi FFS length")
    if header[18:20] != bytes([4, 0x40]):
        raise ValueError("unexpected PreEfi FFS type/attributes")
    data_checksum = header[17]
    header[17] = header[23] = 0
    if sum(header) & 255 or (sum(body[FFS + 24 : FFS_END]) + data_checksum) & 255:
        raise ValueError("invalid PreEfi FFS checksum")


def patch(original, code):
    if len(original) != 0x1F800 or sha(original) != INPUT_SHA:
        raise ValueError("only the authenticated 2.0.4 plaintext body is accepted")
    verify_ffs(original)
    if (
        original[PRE : PRE + 2] != b"MZ"
        or original[PRE + 60 : PRE + 64] != b"\x80\0\0\0"
    ):
        raise ValueError("unexpected PreEfi DOS/PE header")
    if original[STUB : PRE + 0x80] != bytes(64):
        raise ValueError("PreEfi DOS-stub padding is not empty")
    if len(code) != 40:
        raise ValueError("wrapper layout changed; review entry offsets")
    # Fingerprint the audited code independently of the assembler invocation.
    if code.hex() != (
        "10b5032212060023054ca04710bdc046"
        "10b5032212060023024ca04710bdc046"
        "b1f8002251f50022"
    ):
        raise ValueError("wrapper instructions/literals differ from reviewed layout")
    out = bytearray(original)
    out[STUB : STUB + len(code)] = code
    for offset, before, target in CALLS:
        if original[offset : offset + 4].hex() != before:
            raise ValueError("unexpected call instruction")
        out[offset : offset + 4] = thumb_bl(BASE + offset, BASE + target)
    # Recalculate the PreEfi FFS data checksum after patching.
    out[FFS + 17] = -sum(out[FFS + 24 : FFS_END]) & 255
    verify_ffs(out)
    offset, before = MMU_CALL
    if out[offset : offset + 4] != before:
        raise ValueError("physical mapping call was modified")
    allowed = {FFS + 17} | set(range(STUB, STUB + len(code)))
    for offset, _, _ in CALLS:
        allowed.update(range(offset, offset + 4))
    changes = [i for i, (a, b) in enumerate(zip(original, out)) if a != b]
    if not set(changes) <= allowed:
        raise ValueError("unplanned modification")
    return bytes(out), changes
