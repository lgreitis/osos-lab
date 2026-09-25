"""Append the CFW payload and reserve its runtime memory above Apple's BSS."""

import hashlib
import struct
from pathlib import Path

BASE_SHA = "5841de6479f3a655102745fdb20e153ef99c4ef413314ccdb9a4721cace9a36e"
OLD_HEAP = 0x08B32B58
RESOURCE_HOOK = 0x081C7410
INIT_OVERRIDES = 0x08111D0C
TEMPLATE_HOOK = 0x081AEDD8
NEXT_RESOURCE = 0x08111E0C
EXCEPTION_HANDLER = 0x0803930C
SOFTWARE_ABORT = 0x0802606C


def read_layout():
    header = Path(__file__).resolve().parents[2] / "payload/layout.h"
    values = {}
    for line in header.read_text().splitlines():
        if line.startswith("#define CFW_PAYLOAD_"):
            _, name, value = line.split()
            values[name] = int(value, 0)
    return values


LAYOUT = read_layout()
PAYLOAD_BASE = LAYOUT["CFW_PAYLOAD_BASE"]
PAYLOAD_BYTES = LAYOUT["CFW_PAYLOAD_BYTES"]
FILE_BYTES = LAYOUT["CFW_PAYLOAD_FILE_OFFSET"]


def arm_bl(source, target):
    delta = target - source - 8
    if delta % 4 or not -(1 << 25) <= delta < (1 << 25):
        raise ValueError("ARM hook target is out of range or unaligned")
    return struct.pack("<I", 0xEB000000 | ((delta >> 2) & 0xFFFFFF))


def patch(
    original,
    payload,
    hook_address,
    panic_address,
    abort_address,
    template_address,
    settings_action_address,
    eq_hooks,
    game_manifest_address,
):
    if len(original) != FILE_BYTES or hashlib.sha256(original).hexdigest() != BASE_SHA:
        raise ValueError("Expected the original decrypted FW2.0.4 image")
    if len(payload) != PAYLOAD_BYTES:
        raise ValueError("Payload size must match the reserved memory")
    result = bytearray(original)

    def replace(address, expected, replacement):
        position = address - 0x08000000 + 0xB6D8
        if original[position : position + len(expected)] != expected:
            raise ValueError(f"Payload hook preimage mismatch at {address:#x}")
        result[position : position + len(expected)] = replacement

    if (
        game_manifest_address % 4
        or not PAYLOAD_BASE <= game_manifest_address < PAYLOAD_BASE + PAYLOAD_BYTES
    ):
        raise ValueError("Game manifest hook is outside the payload or unaligned")
    replace(
        0x080F9360,
        arm_bl(0x080F9360, 0x0825CCAC),
        arm_bl(0x080F9360, game_manifest_address),
    )

    for destination in eq_hooks.values():
        if (
            destination % 4
            or not PAYLOAD_BASE <= destination < PAYLOAD_BASE + PAYLOAD_BYTES
        ):
            raise ValueError("EQ hook is outside the payload or unaligned")
    for address, original_target, name in (
        (0x080587EC, 0x0809C5E8, "cfw_preferences_hook"),
        (0x08271D0C, 0x08271BF0, "cfw_eq_load_hook"),
        (0x08271D40, 0x08271BF0, "cfw_eq_load_hook"),
    ):
        replace(
            address, arm_bl(address, original_target), arm_bl(address, eq_hooks[name])
        )
    for address, original_target, name in (
        (0x089A61DC, 0x08271F2C, "cfw_eq_process_hook"),
    ):
        replace(
            address,
            struct.pack("<I", original_target),
            struct.pack("<I", eq_hooks[name]),
        )
    replace(
        0x080B27F4,
        bytes.fromhex("640040e2150050e3"),
        struct.pack("<II", 0xE51FF004, eq_hooks["cfw_eq_map_hook"]),
    )
    replace(
        0x081732B4,
        struct.pack("<I", 0xE350006B),
        arm_bl(0x081732B4, eq_hooks["cfw_eq_track_guard"]),
    )
    replace(0x08271CF8, struct.pack("<I", 0xE3510017), struct.pack("<I", 0xE3510018))
    for address, expected in (
        (0x080BB3F4, 0xE3520017),  # Stored ID -> menu index search length.
        (0x081E016C, 0xE3510017),  # Preview image lookup bound.
        (0x081E01B8, 0xE3510017),  # Preset name lookup bound.
        (0x081E0CC0, 0xE3A00017),  # Native preset provider row count.
        (0x08293B38, 0xE3550017),  # Preset string cache iteration.
    ):
        replace(address, struct.pack("<I", expected), struct.pack("<I", expected + 1))
    for address in (0x0807BF14, 0x080BB408):
        replace(
            address,
            struct.pack("<I", 0x083E2568),
            struct.pack("<I", eq_hooks["cfw_eq_preset_ids"]),
        )
    for address in (0x081E01A0, 0x081E01E8, 0x08293CF4):
        replace(
            address,
            struct.pack("<I", 0x089CC500),
            struct.pack("<I", eq_hooks["cfw_eq_preset_resources"]),
        )
    if (
        settings_action_address % 4
        or not PAYLOAD_BASE <= settings_action_address < PAYLOAD_BASE + PAYLOAD_BYTES
    ):
        raise ValueError("Settings action hook is outside the payload or unaligned")
    for address in (0x0899BF4C, 0x0899FE64):
        position = address - 0x08000000 + 0xB6D8
        if struct.unpack_from("<I", original, position)[0] != 0x0821C690:
            raise ValueError("Settings action vtable preimage mismatch")
        struct.pack_into("<I", result, position, settings_action_address)
    for address, target, destination in (
        (RESOURCE_HOOK, INIT_OVERRIDES, hook_address),
        (TEMPLATE_HOOK, NEXT_RESOURCE, template_address),
    ):
        position = address - 0x08000000 + 0xB6D8
        if original[position : position + 4] != arm_bl(address, target):
            raise ValueError(f"UI hook preimage mismatch at {address:#x}")
        if not PAYLOAD_BASE <= destination < PAYLOAD_BASE + PAYLOAD_BYTES:
            raise ValueError("UI hook is outside the payload")
        result[position : position + 4] = arm_bl(address, destination)
    for address, destination, expected in (
        (EXCEPTION_HANDLER, panic_address, "10109fe50400a0e3"),
        (SOFTWARE_ABORT, abort_address, "0010a0e310402de9"),
    ):
        position = address - 0x08000000 + 0xB6D8
        if original[position : position + 8] != bytes.fromhex(expected):
            raise ValueError(f"Panic hook preimage mismatch at {address:#x}")
        if (
            destination % 4
            or not PAYLOAD_BASE <= destination < PAYLOAD_BASE + PAYLOAD_BYTES
        ):
            raise ValueError("Panic handler is outside the payload or unaligned")
        result[position : position + 8] = struct.pack("<II", 0xE51FF004, destination)
    for address in (0x0804B44C, 0x0807BFAC):
        position = address - 0x08000000 + 0xB6D8
        if struct.unpack_from("<I", original, position)[0] != OLD_HEAP:
            raise ValueError("Heap-start preimage mismatch")
        struct.pack_into("<I", result, position, PAYLOAD_BASE + PAYLOAD_BYTES)
    result.extend(payload)
    for position in (0xC, 0x10, 0x14):
        if struct.unpack_from("<I", original, position)[0] != FILE_BYTES - 0x800:
            raise ValueError("OSOS header length mismatch")
        struct.pack_into("<I", result, position, len(result) - 0x800)
    return bytes(result)
