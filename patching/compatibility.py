# SPDX-License-Identifier: GPL-3.0-only
"""Select firmware bindings once, before compiling the shared companion."""

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
    osos = shim_directory(source, "osos", selection["osos"], "osos-handoff.h")
    return CompanionShims(nor, osos)
