#!/usr/bin/env python3
"""Create a local Ghidra project or export its analysis for Git."""

import argparse
import hashlib
import json
import os
import struct
import subprocess
import tempfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "ghidra/analysis"
MANIFEST = ROOT / "ghidra/programs.json"
SCRIPTS = ROOT / "ghidra/scripts"
WORK = ROOT / ".build"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def runtime_elf(body, symbol_prefix):
    """Recreate the ELF wrapper originally imported as raw OSOS memory."""
    strings = bytearray(b"\0")
    symbols = bytearray(16)
    for suffix, value, section in [
        ("start", 0, 1),
        ("end", len(body), 1),
        ("size", len(body), 0xFFF1),
    ]:
        symbols.extend(struct.pack("<IIIBBH", len(strings), value, 0, 0x10, 0, section))
        strings.extend((symbol_prefix + suffix).encode() + b"\0")
    names = b"\0.symtab\0.strtab\0.shstrtab\0.text\0"
    symbol_offset = 52 + len(body)
    string_offset = symbol_offset + len(symbols)
    name_offset = string_offset + len(strings)
    section_offset = name_offset + len(names)
    header = struct.pack(
        "<16sHHIIIIIHHHHHH",
        b"\x7fELF\x01\x01\x01\x61" + bytes(8),
        1,
        40,
        1,
        0,
        0,
        section_offset,
        0,
        52,
        0,
        0,
        40,
        5,
        4,
    )
    sections = [
        (0,) * 10,
        (27, 1, 6, 0, 52, len(body), 0, 0, 1, 0),
        (1, 2, 0, 0, symbol_offset, len(symbols), 3, 1, 4, 16),
        (9, 3, 0, 0, string_offset, len(strings), 0, 0, 1, 0),
        (17, 3, 0, 0, name_offset, len(names), 0, 0, 1, 0),
    ]
    return (
        header
        + body
        + symbols
        + strings
        + names
        + b"".join(struct.pack("<10I", *s) for s in sections)
    )


def memory_image(spec, inputs, modules=None):
    path = inputs / spec["input"]
    if modules and spec["input"].startswith("modules/"):
        stem = path.stem
        candidates = list(modules.glob(stem + ".pe32")) + list(
            modules.glob(stem + "-*.pe32")
        )
        if len(candidates) != 1:
            raise ValueError(f"Expected one {stem} PE module in {modules}")
        path = candidates[0]
    data = path.read_bytes()
    if sha(data) != spec["input_sha256"]:
        raise ValueError(f"Wrong firmware input: {path}")
    if "slice" in spec:
        offset, size = spec["slice"]
        data = data[offset : offset + size]
    if "elf_symbol_prefix" in spec:
        data = runtime_elf(data, spec["elf_symbol_prefix"])
    if "legacy_tail" in spec:
        # Preserve the old raw analysis view's uncorrected final AES block.
        data = data[:-16] + bytes.fromhex(spec["legacy_tail"])
    if sha(data) != spec["memory_sha256"]:
        raise ValueError(
            f"Memory image differs from the saved analysis: {spec['path']}"
        )
    return data


def analysis_directory(spec):
    return ANALYSIS / str(Path(spec["path"]).with_suffix(""))


def normalize_analysis(source, spec):
    data = json.loads(source.read_text())
    run = data["runs"][0]
    for artifact in run.get("artifacts", []):
        artifact["location"]["uri"] = spec["input"]
    groups = defaultdict(list)
    for result in run.pop("results"):
        name = result["ruleId"].lower()
        source_type = result["properties"]["additionalProperties"].get("sourceType")
        # Disassembly and function import recreate default references and symbols.
        if name in ("references", "symbols") and source_type == "DEFAULT":
            continue
        if name == "references":
            source_type = result["properties"]["additionalProperties"].get(
                "sourceType", "other"
            )
            name += "-" + source_type.lower()
        groups[name].append(result)
    destination = analysis_directory(spec)
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "program.json").write_text(json.dumps(data, indent=2) + "\n")
    for name, results in groups.items():
        with (destination / (name + ".jsonl")).open("w") as output:
            for result in results:
                output.write(
                    json.dumps(result, separators=(",", ":"), sort_keys=True) + "\n"
                )
    for path in destination.glob("*.jsonl"):
        if path.stem not in groups:
            path.unlink()


def assemble_analysis(spec, destination):
    directory = analysis_directory(spec)
    data = json.loads((directory / "program.json").read_text())
    results = []
    for path in sorted(directory.glob("*.jsonl")):
        with path.open() as source:
            results.extend(json.loads(line) for line in source if line.strip())
    skipped = set(spec.get("skip_functions", []))
    results = [
        r
        for r in results
        if not (
            r["ruleId"] == "FUNCTIONS"
            and r["properties"]["additionalProperties"].get("location") in skipped
        )
    ]
    data["runs"][0]["results"] = results
    with destination.open("w") as output:
        json.dump(data, output, separators=(",", ":"))


def headless(installation, project, name, options):
    command = [
        str(installation / "support/analyzeHeadless"),
        str(project),
        name,
        "-noanalysis",
        "-max-cpu",
        "8",
        "-scriptPath",
        str(SCRIPTS),
        *options,
    ]
    env = os.environ.copy()
    env.setdefault("GHIDRA_HEADLESS_MAXMEM", "8G")
    result = subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env
    )
    (WORK / "ghidra.log").write_text(result.stdout)
    if result.returncode or "ERROR " in result.stdout:
        raise RuntimeError(
            f"Ghidra failed; see {WORK / 'ghidra.log'}\n{result.stdout[-4000:]}"
        )


def create(args, manifest):
    if (args.project / "osos.gpr").exists():
        raise ValueError(f"Project already exists: {args.project / 'osos.gpr'}")
    with tempfile.TemporaryDirectory(prefix="ghidra-import-", dir=WORK) as temporary:
        directory = Path(temporary)
        groups = defaultdict(list)
        for spec in manifest["programs"]:
            path = Path(spec["path"])
            sarif = directory / (spec["path"] + ".sarif")
            sarif.parent.mkdir(parents=True, exist_ok=True)
            assemble_analysis(spec, sarif)
            Path(str(sarif) + ".bytes").write_bytes(
                memory_image(spec, args.inputs, args.modules)
            )
            groups[path.parent].append(sarif)
        args.project.mkdir(parents=True, exist_ok=True)
        for folder, files in groups.items():
            project_name = (
                "osos" if folder == Path(".") else "osos/" + folder.as_posix()
            )
            print(f"Importing {len(files)} programs into {project_name}...", flush=True)
            headless(
                args.ghidra, args.project, project_name, ["-import", *map(str, files)]
            )
    print(f"Open {args.project / 'osos.gpr'}")


def export(args, manifest):
    with tempfile.TemporaryDirectory(prefix="ghidra-export-", dir=WORK) as temporary:
        directory = Path(temporary)
        headless(
            args.ghidra,
            args.project,
            "osos",
            [
                "-process",
                "*",
                "-recursive",
                "-readOnly",
                "-postScript",
                "ExportAnalysis.java",
                str(directory),
            ],
        )
        expected = {spec["path"] + ".sarif" for spec in manifest["programs"]}
        actual = {
            p.relative_to(directory).as_posix() for p in directory.rglob("*.sarif")
        }
        if actual != expected:
            raise ValueError("Project programs differ from ghidra/programs.json")
        for spec in manifest["programs"]:
            sarif = directory / (spec["path"] + ".sarif")
            if sha(Path(str(sarif) + ".bytes").read_bytes()) != spec["memory_sha256"]:
                raise ValueError(f"Firmware memory was edited: {spec['path']}")
        for spec in manifest["programs"]:
            relative = spec["path"] + ".sarif"
            normalize_analysis(directory / relative, spec)
    print(f"Exported {len(manifest['programs'])} programs to {ANALYSIS}")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog="Save and close the project before exporting. Creation needs the firmware inputs and extracted NOR modules listed in ghidra/programs.json.",
    )
    parser.add_argument("command", choices=["create", "export"])
    parser.add_argument(
        "--ghidra",
        type=Path,
        default=os.environ.get("GHIDRA_INSTALL_DIR"),
        help="Ghidra installation directory, or set GHIDRA_INSTALL_DIR",
    )
    parser.add_argument("--project", type=Path, default=ROOT / "ghidra/project")
    parser.add_argument("--inputs", type=Path, default=ROOT / "inputs")
    parser.add_argument(
        "--modules",
        type=Path,
        help="Directory of extracted NOR PE modules; defaults to inputs/modules",
    )
    args = parser.parse_args()
    if args.ghidra is None:
        parser.error("Set GHIDRA_INSTALL_DIR or supply --ghidra /path/to/ghidra")
    args.ghidra = args.ghidra.resolve()
    args.project = args.project.resolve()
    manifest = json.loads(MANIFEST.read_text())
    properties = (
        (args.ghidra / "Ghidra/application.properties").read_text().splitlines()
    )
    version = next(
        line.split("=", 1)[1]
        for line in properties
        if line.startswith("application.version=")
    )
    if version != manifest["ghidra_version"]:
        parser.error(
            f"Use Ghidra {manifest['ghidra_version']} for these analysis exports"
        )
    WORK.mkdir(exist_ok=True)
    if args.command == "create":
        create(args, manifest)
    else:
        export(args, manifest)


if __name__ == "__main__":
    main()
