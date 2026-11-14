#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check native I/O wrapper ownership and package path boundaries on the host."""

import os
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
output = root.parent / ".build/game-sdk-files-test"
output.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(
    [
        os.environ.get("HOST_CC", "cc"),
        "-std=c11",
        "-g",
        "-O1",
        "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I" + str(root / "include"),
        "-I" + str(root / "runtime"),
        "-include",
        str(root / "tests/native_io.h"),
        str(root / "tests/files_test.c"),
        str(root / "runtime/files.c"),
        str(root / "runtime/directory.c"),
        "-o",
        str(output),
    ],
    check=True,
)
subprocess.run([str(output)], check=True)
