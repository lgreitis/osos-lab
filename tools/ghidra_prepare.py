# SPDX-License-Identifier: GPL-3.0-only
"""Build mapped firmware images from preserved decrypted inputs."""

import argparse
import json
import re
import subprocess
from pathlib import Path

from ghidra_images import elf, osos_regions, pe_regions, region, sha

import firmware


def load_profile(name):
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", name):
        raise ValueError("Invalid analysis target name")
    path = Path(__file__).resolve().parents[1] / "ghidra/profiles" / f"{name}.json"
    if not path.exists():
        raise ValueError(f"Unknown analysis profile: {name}")
    profile = json.loads(path.read_text())
    if profile["target"] != name:
        raise ValueError("Analysis profile identity mismatch")
    return profile


def write_program(out, path, regions, entry, provenance):
    data, segments = elf(regions, entry)
    destination = out / "inputs" / path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    return dict(
        path=path,
        input=path,
        input_sha256=sha(data),
        segments=segments,
        memory_sha256=sha(b"".join(data[o : o + n] for o, n in segments)),
        entry=entry,
        entry_points=[{"address": entry & ~1, "thumb": bool(entry & 1)}],
        thumb_regions=[r["name"] for r in regions if r["flags"] & 1 and entry & 1],
        provenance=provenance,
        regions=[
            {k: v for k, v in r.items() if k != "data"}
            | {"initialized": len(r["data"]), "sha256": sha(r["data"])}
            for r in regions
        ],
    )


def prepare(inputs, out, reprise, manifest=None, profile=None):
    profile = profile or load_profile("firmware-2.0.5")
    version = profile["ipsw"]["version"]
    if out.exists():
        raise ValueError(f"Preparation output already exists: {out}")
    loader = (inputs / "apple-loader.bin").read_bytes()
    firmware.verify(loader, profile["inputs"]["apple-loader.bin"], "apple-loader.bin")
    # Validate the OSOS layout before creating any output.
    osos_regions((inputs / "osos.bin").read_bytes(), profile)
    out.mkdir(parents=True)
    subprocess.run(
        [
            str(reprise.resolve()),
            "firmware",
            "extract-efi",
            "--input",
            str((inputs / "apple-loader.bin").resolve()),
            "--out",
            str(out / "modules"),
        ],
        check=True,
    )
    specs = [prepare_osos(inputs, out, profile)]
    modules = json.loads((out / "modules/modules.json").read_text())
    boot_modules = [m for m in modules if m["name"] in ("PreEfi", "SecCore")]
    specs.extend(
        prepare_module(out, module, version)
        for module in modules
        if module not in boot_modules
    )
    specs.append(prepare_boot(out, loader, boot_modules, profile))
    if manifest is not None and specs != manifest["programs"]:
        raise ValueError("Prepared images differ from the maintained manifest")
    ghidra_version = (
        manifest
        or json.loads(
            (Path(__file__).resolve().parents[1] / "ghidra/programs.json").read_text()
        )
    )["ghidra_version"]
    (out / "programs.json").write_text(
        json.dumps(
            {
                "ghidra_version": ghidra_version,
                "prepared": True,
                "language": "ARM:LE:32:v5t",
                "targets": [{"target": profile["target"], "inputs": "."}],
                "programs": specs,
            },
            indent=2,
        )
        + "\n"
    )
    print(f"Prepared {len(specs)} programs in {out}", flush=True)


def prepare_osos(inputs, out, profile):
    osos = (inputs / "osos.bin").read_bytes()
    regions = osos_regions(osos, profile)
    spec = write_program(
        out,
        f"{profile['ipsw']['version']}/osos.elf",
        regions,
        0x22000000,
        {"input": "osos.bin", "sha256": sha(osos)},
    ) | {"verify_functions": profile["analysis"]["osos_verify_functions"]}
    if "osos_entry_points" in profile["analysis"]:
        spec["entry_points"] = profile["analysis"]["osos_entry_points"]
    return spec


def prepare_module(out, module, version):
    data = (out / "modules" / module["file"]).read_bytes()
    regions, entry, _ = pe_regions(data)
    path = f"{version}/NOR/modules/{module['name']}-{module['guid']}.elf"
    return write_program(
        out,
        path,
        regions,
        entry,
        module
        | {
            "sha256": sha(data),
            "address_basis": "image header; zero base is module-relative",
        },
    )


def prepare_boot(out, loader, modules, profile):
    handoff = profile["analysis"]["nor_handoff_offset"]
    regions = [
        region(".vectors", 0x22000000, loader[:0x100], flags=5, source=0),
        region(
            ".handoff", 0x22000000 + handoff, loader[handoff:], flags=5, source=handoff
        ),
    ]
    entries = [0x22000000]
    entry_points = [{"address": 0x22000000, "thumb": False}]
    thumb_regions = []
    for module in modules:
        data = (out / "modules" / module["file"]).read_bytes()
        parts, entry, _ = pe_regions(data)
        for part in parts:
            part["name"] = "." + module["name"].lower() + part["name"]
            if part["data"]:
                offset = part["address"] - 0x22000000
                if loader[offset : offset + len(part["data"])] != part["data"]:
                    raise ValueError(f"Boot module is not in place: {module['name']}")
                part["source"] = offset
        regions.extend(parts)
        entries.append(entry & ~1)
        entry_points.append({"address": entry & ~1, "thumb": bool(entry & 1)})
        thumb_regions.extend(p["name"] for p in parts if p["flags"] & 1 and entry & 1)
    return write_program(
        out,
        f"{profile['ipsw']['version']}/NOR/boot.elf",
        regions,
        0x22000000,
        {"input": "apple-loader.bin", "sha256": sha(loader), "modules": modules},
    ) | {
        "verify_functions": entries,
        "entry_points": entry_points,
        "thumb_regions": thumb_regions,
    }


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", default="firmware-2.0.5")
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--reprise", type=Path, default=root / "host/target/release/reprise"
    )
    args = parser.parse_args()
    prepare(args.inputs, args.out, args.reprise, profile=load_profile(args.target))


if __name__ == "__main__":
    main()
