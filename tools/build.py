#!/usr/bin/env python3
"""Build the Classic 7G CFW and its matching bootloader."""

import argparse
import importlib.util
import json
import os
import shutil
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import apple_patch
import cfw_info
import setup
from firmware import relocate, sha, sysinfo

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "targets/classic7g-2.0.4.json"
WORK = ROOT / ".build"
APPLE = ROOT / "loader/apple"
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
ROM_MODULES = [
    ("ROM", 0x2201C300, 21),
    ("ClockAndReset", 0x2201D900, 10),
    ("Cpu", 0x2201E180, 26),
    ("InterruptController", 0x2201EB80, 17),
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


def load_patch(name):
    spec = importlib.util.spec_from_file_location(
        name, ROOT / "patches/osos" / (name + ".py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify_inputs(directory, target, names):
    data = {}
    for name in names:
        expected = target["inputs"][name]
        path = directory / name
        if not path.is_file():
            raise ValueError(f"Missing input {path}; use tools/import_inputs.py --help")
        blob = path.read_bytes()
        if len(blob) != expected["bytes"] or sha(blob) != expected["sha256"]:
            raise ValueError(f"Unexpected input: {name}")
        data[name] = blob
    return data


def compile_payload(directory, source, units, linker, output, prefix, jobs):
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
                "-I",
                APPLE,
                "-I",
                source,
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
    listing = run([prefix + "nm", "-n", output + ".elf"], directory)
    symbols = {
        p[2]: int(p[0], 16)
        for line in listing.splitlines()
        if len(p := line.split()) == 3
    }
    run(
        [prefix + "objcopy", "-O", "binary", output + ".elf", output + ".bin"],
        directory,
    )
    return (directory / (output + ".bin")).read_bytes(), symbols


def relocated_module(data, name, address, count):
    blob = data[f"modules/{name}.pe32"]
    return relocate(blob, (name, address, len(blob), count, sha(blob)))[0]


def source_revision():
    try:
        revision = run(["git", "rev-parse", "--short=8", "HEAD"], ROOT).strip()
        changes = run(
            ["git", "status", "--porcelain", "--untracked-files=normal"], ROOT
        )
    except (OSError, RuntimeError):
        return "unknown"
    return revision + ("-dirty" if changes.strip() else "")


def build_cfw_payload(firmware, prefix, jobs):
    source = ROOT / "payload"
    patcher = load_patch("patch_osos_payload")
    units = [
        (p.name, [])
        for p in sorted(source.iterdir())
        if p.suffix in (".c", ".S") and not p.name.endswith(".lds.S")
    ]
    generated = cfw_info.generate(firmware, WORK / "payload", source_revision())
    units.append((str(generated), []))
    code, symbols = compile_payload(
        WORK / "payload", source, units, "payload.lds.S", "payload", prefix, jobs
    )
    if symbols["__payload_start"] != patcher.PAYLOAD_BASE:
        raise ValueError("CFW payload base moved")
    if symbols["__payload_end"] > patcher.PAYLOAD_BASE + patcher.PAYLOAD_BYTES:
        raise ValueError("CFW payload exceeds reserved memory")
    if len(code) > patcher.PAYLOAD_BYTES:
        raise ValueError("CFW payload image exceeds reserved memory")
    return code.ljust(patcher.PAYLOAD_BYTES, b"\0"), symbols


def build_osos(out, data, prefix, jobs):
    print("Patching OSOS...", flush=True)
    payload, symbols = build_cfw_payload(data["osos.bin"], prefix, jobs)
    osos = load_patch("patch_osos_payload").patch(
        data["osos.bin"],
        payload,
        symbols["cfw_resource_hook"],
        symbols["cfw_panic_entry"],
        symbols["cfw_abort_entry"],
        symbols["cfw_template_hook"],
        symbols["cfw_settings_action_hook"],
        {
            name: symbols[name]
            for name in (
                "cfw_preferences_hook",
                "cfw_eq_load_hook",
                "cfw_eq_process_hook",
                "cfw_eq_map_hook",
                "cfw_eq_track_guard",
                "cfw_eq_preset_resources",
                "cfw_eq_preset_ids",
            )
        },
        symbols["cfw_game_manifest_hook"],
    )
    (out / "osos-cfw.bin").write_bytes(osos)


def build_apple(out, data, prefix, jobs):
    print("Building Apple companion loader...", flush=True)
    handoff = WORK / "apple-handoff"
    handoff.mkdir(parents=True, exist_ok=True)
    (handoff / "bds-relocated.bin").write_bytes(
        relocated_module(data, "Bds", 0x0BB24000, 53)
    )
    (handoff / "sysinfo.bin").write_bytes(sysinfo(data["nor.bin"]))
    software = data["modules/SoftwareVersion.pe32"]
    (handoff / "software-version.bin").write_bytes(
        software[0x220:0x242] + bytes(0x1E) + software[0x260:0x264]
    )
    (handoff / "osos-header.bin").write_bytes(data["osos.bin"][:0x800])
    extension, _ = compile_payload(
        handoff,
        APPLE / "handoff",
        [
            (n, [])
            for n in [
                "extension.S",
                "extension.c",
                "bds-combined.c",
                "../common/rom-context.c",
            ]
        ],
        "extension.lds",
        "extension",
        prefix,
        jobs,
    )
    if len(extension) != 0x6800:
        raise ValueError("Unexpected handoff size")

    native = WORK / "apple-native"
    helper, _ = compile_payload(
        native,
        APPLE / "native",
        [(n, []) for n in ["resume.S", "trampoline.S", "hook.c"]],
        "native.lds",
        "native",
        prefix,
        jobs,
    )
    if len(helper) > 0x4000:
        raise ValueError("Native helper overlaps its state")

    startup = WORK / "apple-startup"
    startup.mkdir(parents=True, exist_ok=True)
    for name, address, count in ROM_MODULES:
        (startup / (name + "-relocated.bin")).write_bytes(
            relocated_module(data, name, address, count)
        )
    code, symbols = compile_payload(
        startup,
        APPLE / "startup",
        [
            ("start.S", []),
            ("hook.c", []),
            ("../common/rom-context.c", ["-DSTARTUP_CONTEXT=1"]),
            ("rom-payload.S", []),
            ("files.c", []),
        ],
        "probe.lds",
        "probe",
        prefix,
        jobs,
    )
    run(
        [
            prefix + "gcc",
            "-mcpu=arm926ej-s",
            "-c",
            APPLE / "startup/wrappers.S",
            "-o",
            "wrappers.o",
        ],
        startup,
    )
    run(
        [
            prefix + "objcopy",
            "-O",
            "binary",
            "-j",
            ".text",
            "wrappers.o",
            "wrappers.bin",
        ],
        startup,
    )
    loader, _ = apple_patch.patch(
        data["apple-loader.bin"], code, symbols, (startup / "wrappers.bin").read_bytes()
    )
    (startup / "apple-loader.bin").write_bytes(loader)

    payload = bytearray(0xC000)
    payload[: len(extension)] = extension
    payload[0x8000 : 0x8000 + len(helper)] = helper
    companion = loader + payload
    if len(loader) != 0x1F800 or len(companion) != 0x2B800:
        raise ValueError("Unexpected companion layout")
    (out / "cfw-loader.bin").write_bytes(companion)


def build_rockbox(out, rockbox, target, prefix, jobs):
    print("Building Rockbox bootloader...", flush=True)
    if not (rockbox / "bootloader/cfw-file-ipod6g.c").is_file():
        raise ValueError("Rockbox patch missing; run tools/setup.py")
    directory = WORK / "rockbox"
    directory.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = str(Path(prefix).parent) + os.pathsep + env.get("PATH", "")
    env["VERSION"] = target["rockbox_version"]
    makefile = directory / "Makefile"
    if not makefile.exists():
        (directory / "configure.log").write_text(
            run(
                [rockbox / "tools/configure", "--target=ipod6g", "--type=b"],
                directory,
                env,
            )
        )
        text = makefile.read_text()
        line = next(
            line
            for line in text.splitlines()
            if line.startswith("export EXTRA_DEFINES=")
        )
        makefile.write_text(text.replace(line, line + " -DIPOD_CFW_FILE_BOOT", 1))
    (directory / "build.log").write_text(run(["make", f"-j{jobs}"], directory, env))
    shutil.copyfile(
        directory / "bootloader-ipod6g.ipod", out / "bootloader-ipod6g.ipod"
    )

    packager = WORK / "packager"
    packager.mkdir(parents=True, exist_ok=True)
    sources = rockbox / "utils/mks5lboot"
    command = [
        "cc",
        "-O2",
        "-Wall",
        "-Wextra",
        '-DVERSION="' + target["rockbox_version"] + '"',
        sources / "dualboot.c",
        sources / "mkdfu.c",
        sources / "ipoddfu.c",
        sources / "main.c",
        "-o",
        "mks5lboot",
    ]
    if os.uname().sysname == "Darwin":
        command += ["-framework", "IOKit", "-framework", "CoreFoundation"]
    else:
        command += [
            "-DUSE_LIBUSBAPI",
            *run(["pkg-config", "--cflags", "--libs", "libusb-1.0"], packager).split(),
        ]
    (packager / "build.log").write_text(run(command, packager))
    (packager / "package.log").write_text(
        run(
            [
                packager / "mks5lboot",
                "--mkdfu-inst",
                out / "bootloader-ipod6g.ipod",
                out / "install-rockbox-cfw.dfu",
            ],
            out,
        )
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", type=Path, default=ROOT / "inputs")
    parser.add_argument("--out", type=Path, default=ROOT / "build")
    parser.add_argument(
        "target",
        nargs="?",
        choices=["all", "osos", "loader", "bootloader"],
        default="all",
    )
    parser.add_argument(
        "--cross-prefix", default=os.environ.get("CROSS_COMPILE", "arm-elf-eabi-")
    )
    parser.add_argument("--jobs", type=int, choices=range(1, 9), default=8)
    args = parser.parse_args()
    target = json.loads(TARGET.read_text())
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if args.target in ("all", "osos", "loader"):
        names = ["osos.bin"] if args.target == "osos" else list(target["inputs"])
        data = verify_inputs(args.inputs, target, names)
    compiler = shutil.which(args.cross_prefix + "gcc")
    if not compiler:
        parser.error("ARM GCC missing; supply --cross-prefix /path/to/arm-elf-eabi-")
    prefix = str(Path(compiler).absolute())[:-3]
    if (
        run([prefix + "gcc", "-dumpfullversion"], ROOT).strip()
        != target["toolchain"]["gcc"]
    ):
        parser.error("This target requires ARM GCC " + target["toolchain"]["gcc"])
    if args.target in ("all", "osos"):
        build_osos(out, data, prefix, args.jobs)
    if args.target in ("all", "loader"):
        build_apple(out, data, prefix, args.jobs)
    if args.target in ("all", "bootloader"):
        dependencies = json.loads((ROOT / "dependencies.lock").read_text())
        rockbox = setup.verify("rockbox", dependencies["rockbox"])
        build_rockbox(out, rockbox, target, prefix, args.jobs)
    print(f"Built {args.target}: {out}")


if __name__ == "__main__":
    main()
