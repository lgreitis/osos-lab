# SPDX-License-Identifier: GPL-3.0-only
"""Ghidra preflight, headless execution and project publication."""

import os
import re
import shutil
import subprocess
import tempfile
from contextlib import contextmanager
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / ".build"
SCRIPTS = ROOT / "ghidra/scripts"


def preflight(args, manifest):
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.name):
        raise ValueError(
            "Project name must contain only letters, digits, underscores or hyphens"
        )
    if any(part.startswith(".") for part in args.project.parts):
        raise ValueError("Ghidra project paths cannot contain hidden directories")
    if args.ghidra is None:
        raise ValueError("Set GHIDRA_INSTALL_DIR or supply --ghidra /path/to/ghidra")
    if not os.access(args.ghidra / "support/analyzeHeadless", os.X_OK):
        raise ValueError("Missing executable Ghidra support/analyzeHeadless")
    properties = dict(
        line.split("=", 1)
        for line in (args.ghidra / "Ghidra/application.properties")
        .read_text()
        .splitlines()
        if "=" in line and not line.startswith("#")
    )
    if properties["application.version"] != manifest["ghidra_version"]:
        raise ValueError(f"Use Ghidra {manifest['ghidra_version']} for this manifest")
    check_java(properties)
    check_project(args)


def check_java(properties):
    java_home = os.environ.get("JAVA_HOME")
    java = str(Path(java_home) / "bin/java") if java_home else shutil.which("java")
    if not java:
        raise ValueError("Set JAVA_HOME to a supported JDK")
    result = subprocess.run(
        [java, "-version"], capture_output=True, text=True, check=True
    )
    match = re.search(r'version "(\d+)', result.stderr + result.stdout)
    minimum = int(properties["application.java.min"])
    maximum = properties.get("application.java.max")
    if (
        not match
        or int(match[1]) < minimum
        or (maximum and int(match[1]) > int(maximum))
    ):
        raise ValueError(
            f"Set JAVA_HOME to JDK {minimum}"
            + (f" through {maximum}" if maximum else " or newer")
        )


def check_project(args):
    if args.command == "import":
        if args.project.exists():
            raise ValueError(f"Import requires a new project directory: {args.project}")
        if not args.reprise.is_file() or not os.access(args.reprise, os.X_OK):
            raise ValueError("Build reprise-cli in release mode or supply --reprise")
    else:
        if not (args.project / f"{args.name}.gpr").is_file():
            raise ValueError(f"Missing project: {args.project / args.name}")
        if list(args.project.glob(f"{args.name}.lock*")):
            raise ValueError(
                "Save and close the Ghidra project before exporting (lock present)"
            )


@contextmanager
def staged_project(destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Ghidra rejects hidden path components, including .build.
    with tempfile.TemporaryDirectory(
        prefix="ghidra-import-", dir=destination.parent
    ) as temporary:
        stage = Path(temporary)
        yield stage
        if destination.exists():
            raise ValueError(
                f"Project destination appeared during import: {destination}"
            )
        stage.rename(destination)


def headless(args, project, name, options, phase, markers=()):
    WORK.mkdir(exist_ok=True)
    log = WORK / f"ghidra-{phase}.log"
    command = [
        str(args.ghidra / "support/analyzeHeadless"),
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
    print(f"{phase}: {log}", flush=True)
    with log.open("w") as output:
        result = subprocess.run(
            command, stdout=output, stderr=subprocess.STDOUT, env=env
        )
    output = log.read_text()
    missing = [marker for marker in markers if output.count(marker) != 1]
    # Analyzer diagnostics do not imply a failed import. Scripts and the final
    # verifier must finish, and framework/script errors remain fatal.
    errors = [line for line in output.splitlines() if "ERROR " in line]
    arm_context_conflict = (
        "ERROR Unexpected Exception (ArmAnalyzer) "
        "ghidra.program.model.listing.ContextChangeException: "
        "Context register change conflicts with one or more instructions"
    )
    fatal = [
        line
        for line in errors
        if "(ClearFlowAndRepairCmd)" not in line
        and line.strip() != arm_context_conflict
    ]
    if result.returncode or fatal or missing:
        raise RuntimeError(
            f"Ghidra failed; see {log}\n"
            + "\n".join(fatal[-8:])
            + f"\nMissing/duplicate completion markers: {missing}"
        )
    if errors:
        print(
            f"{phase}: {len(errors)} analyzer diagnostics recorded in {log}", flush=True
        )
    for line in output.splitlines():
        if "> ANALYSIS " in line and ": 0 error bookmarks" not in line:
            print(
                line.split("> ANALYSIS ", 1)[1].split(" (GhidraScript)")[0], flush=True
            )
