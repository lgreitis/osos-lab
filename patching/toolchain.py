# SPDX-License-Identifier: GPL-3.0-only
"""Cross-compile freestanding firmware payloads."""

import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from .symbols import read as read_symbols

FLAGS = [
    "-mcpu=arm926ej-s",
    "-mthumb-interwork",
    "-Os",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-common",
    "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables",
    "-Wall",
    "-Wextra",
    "-Werror",
]


def run(command, cwd, env=None):
    result = subprocess.run(
        list(map(str, command)),
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode:
        raise RuntimeError(f"Command failed: {command}\n{result.stdout}")
    return result.stdout


def compile_payload(
    directory,
    source,
    units,
    linker,
    output,
    prefix,
    jobs,
    includes=(),
    thumb_symbols=False,
):
    directory.mkdir(parents=True, exist_ok=True)
    commands, objects = [], []
    for name, defines in units:
        obj = Path(name).name + ".o"
        objects.append(obj)
        options = ["-mthumb", "-std=c99"] if name.endswith(".c") else []
        commands.append(
            [
                prefix + "gcc",
                *FLAGS,
                *defines,
                *options,
                *[flag for path in includes for flag in ("-I", path)],
                "-I",
                source,
                "-I",
                directory,
                "-Wa,-I," + str(source),
                "-c",
                source / name,
                "-o",
                obj,
            ]
        )
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        log = list(pool.map(lambda command: run(command, directory), commands))
    (directory / "compile.log").write_text("".join(log))
    linker_path = source / linker
    if linker_path.suffix == ".S":
        linker_path = directory / (output + ".lds")
        linker_path.write_text(
            run([prefix + "gcc", "-E", "-P", "-x", "c", source / linker], directory)
        )
    link = [
        prefix + "gcc",
        "-mcpu=arm926ej-s",
        "-mthumb-interwork",
        "-nostdlib",
        "-Wl,-T," + str(linker_path),
        "-Wl,-Map," + output + ".map",
        *objects,
        "-lgcc",
        "-o",
        output + ".elf",
    ]
    (directory / "link.log").write_text(run(link, directory))
    if run([prefix + "nm", "-u", output + ".elf"], directory).strip():
        raise ValueError(f"Undefined symbols in {output}")
    symbols = read_symbols(
        directory / (output + ".elf"), prefix, thumb_bits=thumb_symbols
    )
    run(
        [prefix + "objcopy", "-O", "binary", output + ".elf", output + ".bin"],
        directory,
    )
    return (directory / (output + ".bin")).read_bytes(), symbols
