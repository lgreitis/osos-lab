#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Adapted from RepriseOS game-sdk/tests/run.py (8fc840d) by lgreitis.
"""Check the package path rules, the request lifecycle, the file, folder,
save store, sound, music, clock, settings and device calls, the runtime's
frame and the GL forwarders against host stand-ins, the drawing and random
calls on a small framebuffer, the formatter, the maths, the memory calls and
the module loader, and eapp-pack's module packing:
python3 tests/run.py [SYMBOLS_HEADER]."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
header = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "firmware/osos-2.0.5.h"
if not header.is_file():
    sys.exit(f"{header} not found")
PROGRAMS = {
    "host_test": (
        "request.c",
        "files.c",
        "directory.c",
        "save.c",
        "sound.c",
        "music.c",
        "runtime.c",
        "time.c",
        "input.c",
        "system.c",
        "random.c",
    ),
    "gl_test": ("gl.c", "math.c", "pack.c"),
    "media_test": ("text.c", "users.c", "media.c", "media_image.c"),
    "draw_test": ("draw.c", "random.c", "ui.c", "bitmap.c", "pack.c"),
    "format_test": ("format.c", "text.c"),
    "math_test": ("math.c",),
    "memory_test": (),
    "module_test": ("module.c",),
}
with tempfile.TemporaryDirectory(prefix="eapp-tests-") as name:
    work = Path(name)
    for program, sources in PROGRAMS.items():
        executable = work / program
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
                "-I" + str(root),
                "-I" + str(root / "runtime"),
                "-I" + str(root / "include"),
                "-I" + str(header.parent),
                str(root / "tests" / (program + ".c")),
                *(str(root / "runtime" / source) for source in sources),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)
subprocess.run([sys.executable, str(root / "tests" / "package_test.py")], check=True)
print("eApp SDK host checks passed")
