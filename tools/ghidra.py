#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare and import firmware analysis, or export a closed Ghidra project."""

import argparse
import json
import os
import subprocess
import tempfile
from collections import defaultdict
from pathlib import Path

import target_profiles
from ghidra_analysis import (
    assemble_analysis,
    memory_image,
    normalize_analysis,
    publish_analysis,
    sha,
)
from ghidra_prepare import prepare
from ghidra_project import WORK, headless, preflight, staged_project

ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "ghidra/analysis"
MANIFEST = ROOT / "ghidra/programs.json"


def select_targets(manifest, names):
    available = {source["target"]: source for source in manifest["targets"]}
    if names and (len(names) != len(set(names)) or set(names) - available.keys()):
        raise ValueError(f"Select distinct targets from: {', '.join(available)}")
    sources = [available[name] for name in names] if names else list(available.values())
    versions = {
        target_profiles.load(source["target"])["ipsw"]["version"] for source in sources
    }
    programs = [
        spec for spec in manifest["programs"] if Path(spec["path"]).parts[0] in versions
    ]
    if not programs or {Path(spec["path"]).parts[0] for spec in programs} != versions:
        raise ValueError("Manifest is missing programs for selected targets")
    paths = [spec["path"] for spec in programs]
    if len(paths) != len(set(paths)) or any(
        Path(p).is_absolute() or ".." in Path(p).parts for p in paths
    ):
        raise ValueError("Manifest program paths must be unique and relative")
    return dict(manifest, targets=sources, programs=programs)


def validate_inputs(args):
    raw = any((args.ipsw, args.nor, args.osos, args.apple_loader))
    if args.command == "export":
        if raw or args.inputs or args.fresh:
            raise ValueError("Input and --fresh options apply only to import")
        return
    if args.inputs and len(args.target or []) != 1:
        raise ValueError(
            "--inputs requires exactly one --target and points directly to its decrypted files"
        )
    if raw and (not args.ipsw or not args.nor or not args.osos or args.inputs):
        raise ValueError(
            "Use --ipsw, --nor and --osos together, optionally --apple-loader; omit --inputs. Supply decrypted OSOS and, for encrypted NOR, its decrypted loader. No USB operation is performed."
        )
    if raw and len(args.target or []) > 1:
        raise ValueError("An IPSW import selects exactly one target")


def resolve_ipsw(args):
    result = subprocess.run(
        [str(args.reprise), "firmware", "inspect", "--ipsw", str(args.ipsw)],
        capture_output=True,
        text=True,
        check=True,
    )
    target = json.loads(result.stdout).get("target")
    if not target:
        raise ValueError(
            "IPSW does not match a supported target; rebuild reprise-cli if its inspection output has no target field"
        )
    if args.target and args.target != [target]:
        raise ValueError("IPSW does not match --target")
    args.target = [target]


def prepare_ipsw(args, directory):
    inputs = directory / "plaintext"
    command = [
        str(args.reprise),
        "firmware",
        "prepare",
        "--ipsw",
        str(args.ipsw),
        "--nor",
        str(args.nor),
        "--osos",
        str(args.osos),
        "--out",
        str(inputs),
    ]
    if args.apple_loader:
        command.extend(["--apple-loader", str(args.apple_loader)])
    subprocess.run(command, check=True)
    return inputs


def prepare_images(args, manifest, directory):
    groups = defaultdict(list)
    generated = []
    plaintext = prepare_ipsw(args, directory) if args.ipsw else None
    for source in manifest["targets"]:
        profile = target_profiles.load(source["target"])
        version = profile["ipsw"]["version"]
        specs = [
            spec
            for spec in manifest["programs"]
            if Path(spec["path"]).parts[0] == version
        ]
        prepared = directory / source["target"]
        inputs = plaintext or args.inputs or ROOT / "inputs" / source["inputs"]
        prepare(
            inputs,
            prepared,
            args.reprise,
            manifest=dict(manifest, programs=specs),
            profile=profile,
        )
        generated.extend(
            json.loads((prepared / "programs.json").read_text())["programs"]
        )
        for spec in specs:
            folder = Path(spec["path"]).parent
            if args.fresh:
                file = prepared / "inputs" / spec["input"]
            else:
                file = directory / (spec["path"] + ".sarif")
                file.parent.mkdir(parents=True, exist_ok=True)
                assemble_analysis(spec, file, args.analysis)
                Path(str(file) + ".bytes").write_bytes(
                    memory_image(spec, prepared / "inputs")
                )
            groups[folder].append(file)
    return groups, dict(manifest, programs=generated)


def import_groups(args, project, groups, manifest_path):
    for folder, files in groups.items():
        phase = "import-" + folder.as_posix().replace("/", "-")
        script = "AnalyzePrepared.java" if args.fresh else "ImportAnalysis.java"
        script_args = (
            [str(manifest_path), folder.as_posix()]
            if args.fresh
            else [str(args.analysis / folder)]
        )
        markers = [
            f"PREPARED {folder}/{file.name}"
            if args.fresh
            else f"Analysis imported: {file.name.removesuffix('.sarif')}"
            for file in files
        ]
        headless(
            args,
            project,
            args.name + "/" + folder.as_posix(),
            [
                "-processor",
                "ARM:LE:32:v5t",
                "-cspec",
                "default",
                "-import",
                *map(str, files),
                "-postScript",
                script,
                *script_args,
            ],
            phase,
            markers,
        )


def verify(args, project, manifest, manifest_path, output=None):
    for source in manifest["targets"]:
        version = target_profiles.load(source["target"])["ipsw"]["version"]
        specs = [
            spec
            for spec in manifest["programs"]
            if Path(spec["path"]).parts[0] == version
        ]
        options = [
            "-process",
            "*",
            "-recursive",
            "-readOnly",
            "-postScript",
            "VerifyAnalysis.java",
            str(manifest_path),
        ]
        if output:
            options.extend(
                [
                    "-postScript",
                    "ExportAnalysis.java",
                    str(output),
                    *(s["path"] for s in specs),
                ]
            )
        headless(
            args,
            project,
            args.name + "/" + version,
            options,
            ("export-" if output else "verify-") + version,
            [f"VERIFIED {s['path']}:" for s in specs],
        )


def import_project(args, manifest):
    with tempfile.TemporaryDirectory(prefix="ghidra-inputs-", dir=WORK) as temporary:
        groups, prepared = prepare_images(args, manifest, Path(temporary))
        with staged_project(args.project) as stage:
            manifest_path = stage / f"{args.name}.manifest.json"
            manifest_path.write_text(json.dumps(prepared, indent=2) + "\n")
            import_groups(args, stage, groups, manifest_path)
            verify(args, stage, prepared, manifest_path)
    print(f"Open {args.project / (args.name + '.gpr')}")


def export(args, manifest):
    with tempfile.TemporaryDirectory(prefix="ghidra-export-", dir=WORK) as temporary:
        directory = Path(temporary)
        manifest_path = directory / "programs.json"
        manifest_path.write_text(json.dumps(manifest))
        verify(args, args.project, manifest, manifest_path, directory)
        expected = {spec["path"] + ".sarif" for spec in manifest["programs"]}
        actual = {
            p.relative_to(directory).as_posix() for p in directory.rglob("*.sarif")
        }
        if actual != expected:
            raise ValueError(
                f"Exported program set differs: missing={sorted(expected - actual)}, unexpected={sorted(actual - expected)}"
            )
        for spec in manifest["programs"]:
            file = directory / (spec["path"] + ".sarif.bytes")
            if sha(file.read_bytes()) != spec["memory_sha256"]:
                raise ValueError(f"Firmware memory was edited: {spec['path']}")
        # Normalize all records before changing any saved analysis.
        normalized = directory / "normalized"
        for spec in manifest["programs"]:
            normalize_analysis(directory / (spec["path"] + ".sarif"), spec, normalized)
        publish_analysis(normalized, args.analysis)
    print(f"Exported {len(manifest['programs'])} programs to {args.analysis}")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["import", "export"])
    parser.add_argument("--name", default="osos")
    parser.add_argument(
        "--target", action="append", help="Select a target; repeat for multiple targets"
    )
    parser.add_argument(
        "--fresh",
        action="store_true",
        help="Analyze prepared ELFs instead of restoring saved annotations",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        help="Default: project sidecar for export, maintained manifest for import",
    )
    parser.add_argument("--analysis", type=Path, default=ANALYSIS)
    parser.add_argument(
        "--reprise", type=Path, default=ROOT / "host/target/release/reprise"
    )
    parser.add_argument(
        "--ghidra", type=Path, default=os.environ.get("GHIDRA_INSTALL_DIR")
    )
    parser.add_argument(
        "--project",
        type=Path,
        default=ROOT / "ghidra/project",
        help="Dedicated project directory; must not exist for import",
    )
    parser.add_argument(
        "--inputs", type=Path, help="Decrypted input directory for a single --target"
    )
    for name, help_text in {
        "ipsw": "Supported IPSW archive; selects the target automatically",
        "nor": "Original 1 MiB NOR backup from the matching device",
        "osos": "Decrypted OSOS IMG1 or plaintext body, including AES padding",
        "apple-loader": "Decrypted Apple loader; required when the NOR loader is encrypted",
    }.items():
        parser.add_argument("--" + name, type=Path, help=help_text)
    return parser, parser.parse_args()


def main():
    parser, args = parse_args()
    try:
        for key, value in vars(args).items():
            if isinstance(value, Path):
                setattr(args, key, value.resolve())
        validate_inputs(args)
        sidecar = args.project / f"{args.name}.manifest.json"
        args.manifest = args.manifest or (
            sidecar if args.command == "export" and sidecar.exists() else MANIFEST
        )
        manifest = json.loads(args.manifest.read_text())
        preflight(args, manifest)
        if args.ipsw:
            resolve_ipsw(args)
        manifest = select_targets(manifest, args.target)
        WORK.mkdir(exist_ok=True)
        if args.command == "import":
            import_project(args, manifest)
        else:
            export(args, manifest)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        detail = getattr(error, "stderr", None) or str(error)
        parser.exit(1, f"error: {detail.strip()}\n")


if __name__ == "__main__":
    main()
