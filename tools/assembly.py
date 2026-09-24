#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Export firmware assembly recipes from an engineering build."""

import argparse
import json
import struct
import subprocess
import tempfile
from pathlib import Path

import bundle
import cfw_info
from firmware import sha

ROOT = Path(__file__).resolve().parents[1]
INTERFACE = "classic7g-file-v1"
SYSINFO = 0x1F800 + 0x5900
INPUTS = {
    "osos": "osos.bin",
    "apple_loader": "apple-loader.bin",
    **{
        name.lower(): f"modules/{name}.pe32"
        for name in (
            "ROM",
            "ClockAndReset",
            "Cpu",
            "InterruptController",
            "Bds",
            "SoftwareVersion",
        )
    },
}


class Recipe:
    def __init__(self, inputs):
        self.inputs = inputs
        self.segments = []
        self.data = bytearray()
        self.output = bytearray()
        self.used = set()

    def append(self, kind, data, name=None, offset=0):
        if not data:
            return
        segment = {"kind": kind, "bytes": len(data)}
        if kind == "input":
            if self.inputs[name][offset : offset + len(data)] != data:
                raise ValueError("Input segment mismatch")
            segment.update(name=name, offset=offset)
            self.used.add(name)
        elif kind == "data":
            segment["offset"] = len(self.data)
            self.data.extend(data)
        elif kind != "zero" or any(data):
            raise ValueError("Invalid zero segment")
        if self.segments:
            previous = self.segments[-1]
            if (
                previous["kind"] == kind
                and previous.get("name") == name
                and (
                    kind == "zero"
                    or previous["offset"] + previous["bytes"] == segment["offset"]
                )
            ):
                previous["bytes"] += len(data)
            else:
                self.segments.append(segment)
        else:
            self.segments.append(segment)
        self.output.extend(data)

    def literal(self, data):
        # Keep long padding runs out of the compiled-code asset.
        position = 0
        while position < len(data):
            zero = data.find(bytes(32), position)
            if zero < 0:
                self.append("data", data[position:])
                break
            self.append("data", data[position:zero])
            end = zero + 32
            while end < len(data) and data[end] == 0:
                end += 1
            self.append("zero", data[zero:end])
            position = end

    def delta(self, name, offset, result):
        original = self.inputs[name][offset : offset + len(result)]
        if len(original) != len(result):
            raise ValueError("Delta outside input")
        start = 0
        while start < len(result):
            equal = original[start] == result[start]
            end = start + 1
            while end < len(result) and (original[end] == result[end]) == equal:
                end += 1
            if equal:
                self.append("input", result[start:end], name, offset + start)
            else:
                self.literal(result[start:end])
            start = end

    def save(self, directory, name, expected):
        if self.output != expected:
            raise ValueError("Recipe replay mismatch")
        recipe = {
            "schema": 1,
            "interface": INTERFACE,
            "inputs": {
                key: {"bytes": len(self.inputs[key]), "sha256": sha(self.inputs[key])}
                for key in sorted(self.used)
            },
            "output": {"bytes": len(expected), "sha256": sha(expected)},
            "segments": self.segments,
        }
        path = directory / (name + ".json")
        raw = (
            json.dumps(recipe, sort_keys=True, separators=(",", ":")) + "\n"
        ).encode()
        if len(raw) > 2 * 1024 * 1024:
            raise ValueError("Recipe exceeds 2 MiB")
        path.write_bytes(raw)
        (directory / (name + ".data")).write_bytes(self.data)
        return {"recipe": str(path), "data": str(directory / (name + ".data"))}


def resource_index(firmware):
    index = {}
    bank = cfw_info.BANK_OFFSET
    _, data_offset, count = struct.unpack_from("<III", firmware, bank)
    for i in range(count):
        _, entries, _, table = struct.unpack_from("<IIII", firmware, bank + 12 + i * 16)
        for j in range(entries):
            _, offset, size = struct.unpack_from(
                "<III", firmware, bank + table + j * 12
            )
            start = bank + data_offset + offset
            for position in range(start, start + size - 7):
                index.setdefault(firmware[position : position + 8], position)
    return index


def resource_delta(recipe, data, index):
    firmware = recipe.inputs["osos"]
    position = literal = 0
    while position + 8 <= len(data):
        source = index.get(data[position : position + 8])
        if source is None:
            position += 1
            continue
        recipe.literal(data[literal:position])
        length = 8
        while (
            position + length < len(data)
            and source + length < len(firmware)
            and data[position + length] == firmware[source + length]
        ):
            length += 1
        recipe.append("input", data[position : position + length], "osos", source)
        position += length
        literal = position
    recipe.literal(data[literal:])


def osos_recipe(inputs, image, work, nm):
    base = len(inputs["osos"])
    payload = bundle.read(work / "payload/payload.bin", 0x100000)
    if image[base:] != payload.ljust(0x100000, b"\0"):
        raise ValueError("OSOS and compiled payload belong to different builds")
    recipe = Recipe(inputs)
    recipe.delta("osos", 0, image[:base])
    listing = subprocess.check_output(
        [nm, "-S", "-n", str(work / "payload/payload.elf")], text=True
    )
    ranges = []
    for line in listing.splitlines():
        fields = line.split()
        if len(fields) == 4 and (
            fields[3].startswith("resource_")
            and fields[3][9:].isdigit()
            or fields[3] == "cfw_eq_preset_resources"
        ):
            offset = int(fields[0], 16) - 0x08B33000 + base
            ranges.append((offset, int(fields[1], 16), fields[3]))
    if not ranges:
        raise ValueError("Missing compiled UI resources")
    index = resource_index(inputs["osos"])
    position = base
    for offset, size, name in sorted(ranges):
        if offset < position or offset + size > len(image):
            raise ValueError("Resource symbol outside payload")
        recipe.literal(image[position:offset])
        if name == "cfw_eq_preset_resources":
            if size != 48 * 4:
                raise ValueError("Unexpected EQ preset table size")
            source = 0x089CC500 - 0x08000000 + 0xB6D8
            recipe.delta("osos", source, image[offset : offset + 46 * 4])
            recipe.literal(image[offset + 46 * 4 : offset + 47 * 4])
            recipe.delta(
                "osos", source + 17 * 4, image[offset + 47 * 4 : offset + size]
            )
        else:
            resource_delta(recipe, image[offset : offset + size], index)
        position = offset + size
    recipe.literal(image[position:])
    return recipe


def companion_recipe(inputs, image):
    if len(image) != 0x2B800:
        raise ValueError("Unexpected companion layout")
    canonical = bytearray(image)
    canonical[SYSINFO : SYSINFO + 0x120] = bytes(0x120)
    recipe = Recipe(inputs)
    regions = [(0, "apple_loader", 0, 0x1A730)]
    for name, start in (
        ("rom", 0x1C300),
        ("clockandreset", 0x1D900),
        ("cpu", 0x1E180),
        ("interruptcontroller", 0x1EB80),
    ):
        regions.append((start, name, 0, len(inputs[name])))
    regions += [
        (0x1F510, "apple_loader", 0x1F510, 0x2F0),
        (0x23800, "bds", 0, 0x1500),
        (0x25000, "softwareversion", 0x220, 0x22),
        (0x25040, "softwareversion", 0x260, 4),
        (0x25800, "osos", 0, 0x800),
    ]
    position = 0
    for start, name, offset, size in sorted(regions):
        if start < position:
            raise ValueError("Overlapping companion regions")
        recipe.literal(canonical[position:start])
        recipe.delta(name, offset, canonical[start : start + size])
        position = start + size
    recipe.literal(canonical[position:])
    return recipe, bytes(canonical)


def nor_descriptor(build_dir, image):
    ipod = bundle.read(build_dir / "bootloader-ipod6g.ipod", 0x20000)
    code = ipod[8:]
    padded = (len(code) + 15) & ~15
    offset = len(image) - padded
    header = offset - 0x800
    if (
        ipod[4:8] != b"ip6g"
        or int.from_bytes(ipod[:4], "big") != (71 + sum(code)) & 0xFFFFFFFF
        or not code
        or header < 0x310
        or image[:8] != b"87021.0\x03"
        or image[header : header + 8] != b"87021.0\x02"
        or struct.unpack_from("<I", image, header + 12)[0] != padded
        or image[header + 0x40 : header + 0x50] != bytes(16)
        or image[offset:] != code.ljust(padded, b"\0")
    ):
        raise ValueError("Expected a matching Rockbox dual-boot installer")
    return {
        "schema": 1,
        "interface": INTERFACE,
        "bytes": len(image),
        "sha256": sha(image),
        "bootloader_offset": offset,
    }


def export(inputs_dir, build_dir, work, helper, version, minimum, out, nm):
    target = json.loads((ROOT / "targets/classic7g-2.0.4.json").read_text())
    inputs = {}
    for name, filename in INPUTS.items():
        data = bundle.read(inputs_dir / filename, bundle.MAX_ASSET)
        if target["inputs"][filename] != {"bytes": len(data), "sha256": sha(data)}:
            raise ValueError(f"Unexpected Apple input: {filename}")
        inputs[name] = data
    osos = bundle.read(build_dir / "osos-cfw.bin", 0xC00000)
    companion = bundle.read(build_dir / "cfw-loader.bin", 0x3FC000)
    nor = bundle.read(build_dir / "install-rockbox-cfw.dfu", 0x20000)
    nor_spec = nor_descriptor(build_dir, nor)
    spec = bundle.helper_spec(helper, version, minimum)
    spec["purpose"] = "release"
    with tempfile.TemporaryDirectory(prefix="reprise-recipes-") as temporary:
        directory = Path(temporary)
        recipes = {"osos": osos_recipe(inputs, osos, work, nm)}
        recipes["companion"], canonical = companion_recipe(inputs, companion)
        for name, expected in (("osos", osos), ("companion", canonical)):
            spec["components"][name] = {
                "format": bundle.FORMATS[name][0],
                "files": recipes[name].save(directory, name, expected),
            }
        descriptor = directory / "nor.json"
        descriptor.write_text(json.dumps(nor_spec) + "\n")
        spec["components"]["nor"] = {
            "format": bundle.FORMATS["nor"][0],
            "files": {
                "image": str((build_dir / "install-rockbox-cfw.dfu").resolve()),
                "descriptor": str(descriptor),
            },
        }
        return bundle.export(spec, directory, out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", type=Path, default=ROOT / "inputs")
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--work", type=Path, default=ROOT / ".build")
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--minimum-installer-version", default="0.1.0")
    parser.add_argument("--nm", default="arm-elf-eabi-nm")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    export(
        args.inputs,
        args.build,
        args.work,
        args.helper,
        args.version,
        args.minimum_installer_version,
        args.out,
        args.nm,
    )
    print(f"Exported firmware bundle: {args.out}")


if __name__ == "__main__":
    main()
