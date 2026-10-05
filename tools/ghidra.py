#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Import a Ghidra project from saved analysis or export its analysis for Git."""

import argparse
import hashlib
import json
import os
import subprocess
import tempfile
from collections import defaultdict
from pathlib import Path

from ghidra_prepare import prepare

ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "ghidra/analysis"
MANIFEST = ROOT / "ghidra/programs.json"
SCRIPTS = ROOT / "ghidra/scripts"
WORK = ROOT / ".build"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def memory_image(spec, inputs):
    path = inputs / spec["input"]
    data = path.read_bytes()
    if sha(data) != spec["input_sha256"]:
        raise ValueError(f"Wrong firmware input: {path}")
    data = b"".join(data[offset : offset + size] for offset, size in spec["segments"])
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
    data["runs"][0]["results"] = results
    # Restore thunks after every target function exists; SARIF imports
    # otherwise lose forward references and unresolved external targets.
    for result in results:
        if result["ruleId"] == "FUNCTIONS":
            props = result["properties"]["additionalProperties"]
            props["isThunk"] = False
            props.pop("thunkAddress", None)
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


def import_files(directory, manifest, inputs):
    groups = defaultdict(list)
    for spec in manifest["programs"]:
        path = Path(spec["path"])
        sarif = directory / (spec["path"] + ".sarif")
        sarif.parent.mkdir(parents=True, exist_ok=True)
        assemble_analysis(spec, sarif)
        Path(str(sarif) + ".bytes").write_bytes(memory_image(spec, inputs))
        groups[path.parent].append(sarif)
    return groups


def import_project(args, manifest):
    if (args.project / (args.name + ".gpr")).exists():
        raise ValueError(f"Project already exists: {args.project}")
    with tempfile.TemporaryDirectory(prefix="ghidra-import-", dir=WORK) as temporary:
        directory = Path(temporary)
        prepared = directory / "prepared"
        prepare(args.inputs, prepared, args.reprise, manifest)
        groups = import_files(directory, manifest, prepared / "inputs")
        args.project.mkdir(parents=True, exist_ok=True)
        for folder, files in groups.items():
            project_name = (
                args.name
                if folder == Path(".")
                else args.name + "/" + folder.as_posix()
            )
            print(f"Importing {len(files)} programs into {project_name}...", flush=True)
            headless(
                args.ghidra,
                args.project,
                project_name,
                [
                    "-import",
                    *map(str, files),
                    "-postScript",
                    "ImportAnalysis.java",
                    str(ANALYSIS / folder),
                ],
            )
    headless(
        args.ghidra,
        args.project,
        args.name,
        [
            "-process",
            "*",
            "-recursive",
            "-readOnly",
            "-postScript",
            "VerifyAnalysis.java",
            str(args.manifest.resolve()),
        ],
    )
    print(f"Open {args.project / (args.name + '.gpr')}")


def export(args, manifest):
    with tempfile.TemporaryDirectory(prefix="ghidra-export-", dir=WORK) as temporary:
        directory = Path(temporary)
        headless(
            args.ghidra,
            args.project,
            args.name,
            [
                "-process",
                "*",
                "-recursive",
                "-readOnly",
                "-postScript",
                "VerifyAnalysis.java",
                str(args.manifest.resolve()),
                "-postScript",
                "ExportAnalysis.java",
                str(directory),
                *(spec["path"] for spec in manifest["programs"]),
            ],
        )
        expected = {spec["path"] + ".sarif" for spec in manifest["programs"]}
        actual = {
            p.relative_to(directory).as_posix() for p in directory.rglob("*.sarif")
        }
        if actual != expected:
            raise ValueError(
                f"Project programs differ from ghidra/programs.json: "
                f"missing={sorted(expected - actual)}, unexpected={sorted(actual - expected)}"
            )
        for spec in manifest["programs"]:
            sarif = directory / (spec["path"] + ".sarif")
            if sha(Path(str(sarif) + ".bytes").read_bytes()) != spec["memory_sha256"]:
                raise ValueError(f"Firmware memory was edited: {spec['path']}")
        for spec in manifest["programs"]:
            relative = spec["path"] + ".sarif"
            normalize_analysis(directory / relative, spec)
    print(f"Exported {len(manifest['programs'])} programs to {ANALYSIS}")


def main():
    global ANALYSIS
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog="Save and close the project before exporting. Import prepares firmware images automatically and requires a new project.",
    )
    parser.add_argument("command", choices=["import", "export"])
    parser.add_argument("--name", default="osos", help="Ghidra project name")
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--analysis", type=Path, default=ANALYSIS)
    parser.add_argument(
        "--reprise", type=Path, default=ROOT / "host/target/debug/reprise"
    )
    parser.add_argument(
        "--ghidra",
        type=Path,
        default=os.environ.get("GHIDRA_INSTALL_DIR"),
        help="Ghidra installation directory, or set GHIDRA_INSTALL_DIR",
    )
    parser.add_argument("--project", type=Path, default=ROOT / "ghidra/project")
    parser.add_argument(
        "--inputs",
        type=Path,
        default=ROOT / "inputs",
        help="Preserved decrypted firmware directory used for import",
    )
    args = parser.parse_args()
    ANALYSIS = args.analysis.resolve()
    manifest = json.loads(args.manifest.read_text())
    if args.ghidra is None:
        parser.error("Set GHIDRA_INSTALL_DIR or supply --ghidra /path/to/ghidra")
    args.ghidra = args.ghidra.resolve()
    args.project = args.project.resolve()
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
    if args.command == "import":
        import_project(args, manifest)
    else:
        export(args, manifest)


if __name__ == "__main__":
    main()
