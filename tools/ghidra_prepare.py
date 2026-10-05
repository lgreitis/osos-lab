# SPDX-License-Identifier: GPL-3.0-only
"""Build mapped firmware images from preserved decrypted inputs."""

import json
import subprocess

from ghidra_images import elf, osos_regions, pe_regions, region, sha


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
        provenance=provenance,
        regions=[
            {k: v for k, v in r.items() if k != "data"}
            | {"initialized": len(r["data"]), "sha256": sha(r["data"])}
            for r in regions
        ],
    )


def prepare(inputs, out, reprise, manifest):
    if out.exists():
        raise ValueError(f"Preparation output already exists: {out}")
    loader = (inputs / "apple-loader.bin").read_bytes()
    if (
        sha(loader)
        != "7caf3863376cf7890adc73601d24fbf11bc9ce89c7b40366fd61b45a1d6633e8"
    ):
        raise ValueError("Unknown loader layout")
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
    specs = [prepare_osos(inputs, out)]
    modules = json.loads((out / "modules/modules.json").read_text())
    boot_modules = [m for m in modules if m["name"] in ("PreEfi", "SecCore")]
    specs.extend(
        prepare_module(out, module) for module in modules if module not in boot_modules
    )
    specs.append(prepare_boot(out, loader, boot_modules))
    if specs != manifest["programs"]:
        raise ValueError("Prepared images differ from the maintained manifest")
    (out / "programs.json").write_text(
        json.dumps(
            {
                "ghidra_version": manifest["ghidra_version"],
                "prepared": True,
                "programs": specs,
            },
            indent=2,
        )
        + "\n"
    )
    print(f"Prepared {len(specs)} programs in {out}", flush=True)


def prepare_osos(inputs, out):
    osos = (inputs / "osos.bin").read_bytes()
    regions = osos_regions(osos)
    return write_program(
        out,
        "2.0.4/osos.elf",
        regions,
        0x22000000,
        {"input": "osos.bin", "sha256": sha(osos)},
    )


def prepare_module(out, module):
    data = (out / "modules" / module["file"]).read_bytes()
    regions, entry, _ = pe_regions(data)
    path = f"2.0.4/NOR/modules/{module['name']}-{module['guid']}.elf"
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


def prepare_boot(out, loader, modules):
    regions = [
        region(".vectors", 0x22000000, loader[:0x100], flags=5, source=0),
        region(".handoff", 0x2201F7BC, loader[0x1F7BC:], flags=5, source=0x1F7BC),
    ]
    entries = [0x22000000]
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
    return write_program(
        out,
        "2.0.4/NOR/boot.elf",
        regions,
        0x22000000,
        {"input": "apple-loader.bin", "sha256": sha(loader), "modules": modules},
    ) | {"verify_functions": entries}
