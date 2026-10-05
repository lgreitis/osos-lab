# SPDX-License-Identifier: GPL-3.0-only
"""Shared target profiles for firmware packaging and analysis preparation."""

import hashlib
import json
import re
import subprocess
from pathlib import Path

DIRECTORY = Path(__file__).resolve().parents[1] / "targets"
DEFAULT_TARGET = "classic7g-2.0.4"


def load(name=DEFAULT_TARGET):
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", name):
        raise ValueError("Invalid target name")
    profile = json.loads((DIRECTORY / f"{name}.json").read_text())
    if profile["target"] != name or profile["compatibility"]["target"] != name:
        raise ValueError("Target profile identity mismatch")
    return profile


def verify(data, fingerprint, name):
    if (
        len(data) != fingerprint["bytes"]
        or hashlib.sha256(data).hexdigest() != fingerprint["sha256"]
    ):
        raise ValueError(f"Input does not match target profile: {name}")


def verify_nor(path, profile, reprise):
    if path.stat().st_size != 0x100000:
        raise ValueError("NOR backup must be exactly 1 MiB")
    result = subprocess.run(
        [str(reprise.resolve()), "syscfg", str(path.resolve()), "--json"],
        check=True,
        capture_output=True,
        text=True,
    )
    identity = json.loads(result.stdout)
    compatibility = profile["compatibility"]
    if (
        identity["model"] not in compatibility["models"]
        or identity["hardware_version"] != compatibility["hardware_version"]
        or identity["recorded_firmware"] != compatibility["apple_firmware"]
    ):
        raise ValueError("NOR identity does not match target profile")
