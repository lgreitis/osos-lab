# SPDX-License-Identifier: GPL-3.0-only
"""Lossless firmware bytes and normalized SARIF records."""

import hashlib
import json
import shutil
import tempfile
from collections import defaultdict
from pathlib import Path


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


def analysis_directory(spec, analysis):
    return analysis / str(Path(spec["path"]).with_suffix(""))


def normalize_analysis(source, spec, analysis):
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
    destination = analysis_directory(spec, analysis)
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


def assemble_analysis(spec, destination, analysis):
    directory = analysis_directory(spec, analysis)
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


def publish_analysis(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    previous = destination.with_name(destination.name + ".export-previous")
    if previous.exists():
        raise ValueError(f"Previous export recovery directory exists: {previous}")
    with tempfile.TemporaryDirectory(
        prefix="ghidra-export-", dir=destination.parent
    ) as temporary:
        ready = Path(temporary) / "ready"
        if destination.exists():
            shutil.copytree(destination, ready)
        else:
            ready.mkdir()
        for program in source.rglob("program.json"):
            relative = program.parent.relative_to(source)
            target = ready / relative
            target.mkdir(parents=True, exist_ok=True)
            for file in target.glob("*.jsonl"):
                file.unlink()
            shutil.copytree(program.parent, target, dirs_exist_ok=True)
        if destination.exists():
            destination.rename(previous)
        try:
            ready.rename(destination)
        except OSError:
            if previous.exists():
                previous.rename(destination)
            raise
        if previous.exists():
            shutil.rmtree(previous)
