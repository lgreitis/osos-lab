"""Patch Apple startup hooks and loader padding."""

import struct

import reservation as r

PAD = 0x1A730
PAD_END = 0x1F510


def arm_b(source, target):
    delta = target - source - 8
    if delta % 4 or not -(1 << 25) <= delta < (1 << 25):
        raise ValueError("ARM branch out of range/alignment")
    return struct.pack("<I", 0xEA000000 | ((delta >> 2) & 0xFFFFFF))


def patch(original, code, symbols, wrappers):
    out = bytearray(r.patch(original, wrappers)[0])
    if not code or len(code) > PAD_END - PAD:
        raise ValueError("startup code does not fit loader padding")
    if (
        original[0x1A718:PAD].hex()
        != "d4548f0722cc48409e94879c214d562f4f5af000f84d00f8"
    ):
        raise ValueError("unexpected padding FFS header")
    if original[PAD:PAD_END] != b"\xff" * (PAD_END - PAD):
        raise ValueError("padding contains unexpected data")
    for offset, before, target in [
        (0x10574, "6a4602a9", "cold_guard"),
        (0xF64E, "01f03ce8", "before_dxe"),
    ]:
        if original[offset : offset + 4].hex() != before:
            raise ValueError("startup hook fingerprint mismatch")
        destination = symbols[target] & ~1
        if not 0x22000000 + PAD <= destination < 0x22000000 + PAD + len(code):
            raise ValueError("hook destination outside startup code")
        out[offset : offset + 4] = r.thumb_bl(0x22000000 + offset, destination)
    for i in range(8):
        target = symbols["probe_start" if i == 0 else "probe_exception"]
        out[i * 4 : i * 4 + 4] = arm_b(0x22000000 + i * 4, target)
    out[PAD : PAD + len(code)] = code
    out[r.FFS + 17] = -sum(out[r.FFS + 24 : r.FFS_END]) & 255
    r.verify_ffs(out)
    # Restrict changes to vectors, startup hooks, padding and the FFS checksum.
    allowed = set(range(32)) | set(range(PAD, PAD + len(code)))
    allowed |= {r.FFS + 17} | set(range(r.STUB, r.STUB + 40))
    for offset in (0x10574, 0xF64E, 0x10596, 0x105BE):
        allowed.update(range(offset, offset + 4))
    changed = [i for i, (a, b) in enumerate(zip(original, out)) if a != b]
    if not set(changed) <= allowed:
        raise ValueError("unexpected changed byte")
    return bytes(out), changed
