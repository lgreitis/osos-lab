# SPDX-License-Identifier: GPL-3.0-only
"""Pinned Apple firmware inputs for the single Classic build."""

import hashlib
import json
from pathlib import Path

DIRECTORY = Path(__file__).resolve().parents[1] / "firmware"
PROFILE = DIRECTORY / "apple.json"


def load():
    return json.loads(PROFILE.read_text())


def verify(data, fingerprint, name):
    if (
        len(data) != fingerprint["bytes"]
        or hashlib.sha256(data).hexdigest() != fingerprint["sha256"]
    ):
        raise ValueError(f"Input does not match firmware: {name}")
