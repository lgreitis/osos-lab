# SPDX-License-Identifier: GPL-3.0-only
"""Select native firmware bindings before compiling payloads and companions."""

import json
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class CompanionShims:
    nor: Path
    osos: Path

    @property
    def includes(self):
        return self.nor, self.osos


def shim_directory(source, kind, name, header):
    if (
        not isinstance(name, str)
        or not name
        or name in (".", "..")
        or Path(name).name != name
    ):
        raise ValueError(f"Invalid {kind} shim selection")
    directory = source / "compat" / kind / name
    if not (directory / header).is_file():
        raise ValueError(f"Missing {kind} shim: {name}")
    return directory


def companion_shims(source, target_path):
    target = json.loads(target_path.read_text())
    selection = target["shims"]
    nor = shim_directory(source, "nor", selection["nor"], "nor-target.h")
    osos = payload_shim(source.parents[1] / "payload", target_path)
    return CompanionShims(nor, osos)


def payload_shim(source, target_path):
    target = json.loads(target_path.read_text())
    return shim_directory(source, "osos", target["shims"]["osos"], "osos-target.h")
