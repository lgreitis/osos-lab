#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the real core and port using an original synthetic ROM and host SDK."""

import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
output = ROOT / "build/port-test"
output.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(
    [
        os.environ.get("HOST_CC", "cc"),
        "-std=c11",
        "-g",
        "-O1",
        "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all",
        "-Wno-deprecated-non-prototype",
        "-Wno-unused-function",
        "-x",
        "c",
        "-I" + str(ROOT.parent / "game-sdk/include"),
        "-I" + str(ROOT / "vendor/infones/src"),
        "-I" + str(ROOT / "src"),
        str(ROOT / "tests/port_test.c"),
        *[str(ROOT / "src" / p) for p in ("nes_app.c", "rom.c", "mappers.c")],
        *[
            str(ROOT.parent / "game-sdk/runtime" / p)
            for p in ("text.c", "ui.c", "wheel.c")
        ],
        *[
            str(ROOT / "vendor/infones/src" / p)
            for p in ("InfoNES.cpp", "InfoNES_pAPU.cpp", "K6502.cpp")
        ],
        "-o",
        str(output),
    ],
    check=True,
)
subprocess.run([str(output)], check=True)
