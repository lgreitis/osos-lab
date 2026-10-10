#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the Classic streaming upload helper."""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))

import firmware  # noqa: E402
from build import compiler_prefix  # noqa: E402

ROM_SHA = firmware.load()["compatibility"]["bootrom_sha256"]
NONCE = b"REPRISE-NONCE-01"
COLD_TAG = b"\xfe\xff\xff\xeaREPRISE-ROM\0"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wrap(body):
    body += bytes(-len(body) % 16)
    # Preserve the result at 0x2201f000, including the incoming IMG1 header.
    if not body or len(body) + 0x800 > 0x1F000:
        raise ValueError("image overlaps the result record")
    header = bytearray(0x800)
    struct.pack_into(
        "<4s3sBIIIII",
        header,
        0,
        b"8702",
        b"1.0",
        2,
        0,
        len(body),
        len(body),
        len(body),
        0,
    )
    image = bytes(header) + body
    if image.count(NONCE) != 1:
        raise ValueError("expected one nonce slot")
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, choices=range(1, 9), default=8)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--rockbox", type=Path, required=True)
    parser.add_argument(
        "--toolchain",
        type=Path,
        required=True,
        help="directory containing arm-elf-eabi-gcc",
    )
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    prefix = compiler_prefix(
        str(args.toolchain.resolve() / "arm-elf-eabi-"),
        firmware.load()["toolchain"]["gcc"],
    )
    blob, cold_offset = build_return(out, prefix)
    source = prepare_source(out, args.rockbox.resolve())
    symbols = compile_helper(out, source, prefix, args.jobs)
    write_image(out, symbols, blob, cold_offset)


def build_runner(out, prefix):
    env = dict(os.environ, PATH=str(Path(prefix).parent) + ":" + os.environ["PATH"])

    def run(command, log):
        with (out / log).open("w") as stream:
            subprocess.run(
                list(map(str, command)),
                cwd=out,
                env=env,
                stdout=stream,
                stderr=subprocess.STDOUT,
                check=True,
            )

    return run


def build_return(out, prefix):
    run = build_runner(out, prefix)
    for name in (
        "main.c",
        "file.c",
        "usb.c",
        "winusb.h",
        "usb_identity.h",
        "upload.h",
        "record.h",
        "layout.h",
        "return.c",
        "return.S",
        "sha256.c",
        "sha256.h",
        "layout.c",
        "memory.h",
        "return.lds",
    ):
        shutil.copyfile(HERE / name, out / name)
    flags = [
        "-mcpu=arm926ej-s",
        "-marm",
        "-Os",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-stack-protector",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-nostdlib",
    ]
    run(
        [
            prefix + "gcc",
            *flags,
            "-Wl,-T,return.lds,-Map,return.map",
            "return.S",
            "return.c",
            "-o",
            "return.elf",
        ],
        "return-build.log",
    )
    run(
        [prefix + "objcopy", "-O", "binary", "return.elf", "return.bin"],
        "return-copy.log",
    )
    blob = (out / "return.bin").read_bytes()
    symbols = subprocess.check_output(
        [prefix + "nm", "-n", str(out / "return.elf")], text=True
    )
    cold = next(
        int(v[0], 16)
        for line in symbols.splitlines()
        if len(v := line.split()) == 3 and v[2] == "cold_init"
    )
    cold_offset = cold - 0x22010000
    if blob[cold_offset : cold_offset + 1024] != COLD_TAG + bytes(1024 - len(COLD_TAG)):
        raise ValueError("invalid ROM patch slot")
    (out / "return_blob.h").write_text(
        "static const unsigned char return_blob[] __attribute__((aligned(4))) = {\n"
        + ",".join(hex(b) for b in blob)
        + "\n};\n"
    )
    return blob, cold_offset


def prepare_source(out, rockbox):
    source = out / "rockbox"
    files = subprocess.check_output(
        [
            "git",
            "-C",
            str(rockbox),
            "ls-files",
            "-z",
            "--cached",
            "--others",
            "--exclude-standard",
        ]
    ).split(b"\0")
    for item in files:
        if not item:
            continue
        src, dest = rockbox / os.fsdecode(item), source / os.fsdecode(item)
        if not src.is_file():
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)
    (source / "bootloader/SOURCES").write_text(
        "common.c\nformat.c\nsnprintf.c\nipod-s5l87xx.c\nupload-file.c\nsha256.c\nlayout.c\n"
    )
    for src, dst in [
        ("main.c", "ipod-s5l87xx.c"),
        ("file.c", "upload-file.c"),
        ("sha256.c", "sha256.c"),
        ("sha256.h", "sha256.h"),
        ("upload.h", "upload.h"),
        ("record.h", "record.h"),
        ("layout.h", "layout.h"),
        ("layout.c", "layout.c"),
        ("memory.h", "memory.h"),
        ("return_blob.h", "return_blob.h"),
    ]:
        shutil.copyfile(out / src, source / "bootloader" / dst)
    (source / "firmware/usb.c").write_text(
        '#include "usb.h"\nvoid usb_acknowledge(long id, intptr_t seqnum) { (void)id; (void)seqnum; }\n'
    )
    shutil.copyfile(out / "usb.c", source / "firmware/usbstack/usb_storage.c")
    shutil.copyfile(out / "upload.h", source / "firmware/usbstack/upload.h")
    for name in ("winusb.h", "usb_identity.h", "memory.h", "layout.h"):
        shutil.copyfile(out / name, source / "firmware/usbstack" / name)
    # The copy may be inside osos-lab; do not discover its parent Git checkout.
    env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(out))
    for flags in (["--check"], []):
        subprocess.run(
            ["git", "apply", *flags, str(HERE / "rockbox.patch")],
            cwd=source,
            env=env,
            check=True,
        )
    return source


def compile_helper(out, source, prefix, jobs):
    run = build_runner(out, prefix)
    run([source / "tools/configure", "--target=ipod6g", "--type=b"], "configure.log")
    run(["make", f"-j{jobs}"], "build.log")
    if "warning:" in (out / "build.log").read_text():
        raise ValueError("compiler warnings; inspect build.log")
    return read_symbols(prefix, out / "bootloader.elf")


def read_symbols(prefix, image):
    text = subprocess.check_output([prefix + "nm", "-n", str(image)], text=True)
    return {
        v[2]: int(v[0], 16) for line in text.splitlines() if len(v := line.split()) == 3
    }


def write_image(out, symbols, blob, cold_offset):
    if (
        not {
            "upload_write_allowed",
            "upload_usb",
            "upload_verify_step",
            "ata_write_sectors",
        }
        <= symbols.keys()
    ):
        raise ValueError("missing upload/write guard symbols")
    if {"launch_onb", "cfw_run_file", "load_firmware", "flsh_write"} & symbols.keys():
        raise ValueError("unexpected boot/flash operation linked")
    if not 0x22020800 <= symbols["_movestart"] < symbols["_fiqstackend"] <= 0x22040000:
        raise ValueError("unexpected helper IRAM layout")
    if not 0x08000000 <= symbols["_end"] <= 0x09800000:
        raise ValueError("helper BSS overlaps the USB buffer")
    data = wrap((out / "bootloader.bin").read_bytes())
    tag = b"REPRISE-UPLOAD2\0"
    if data.count(tag) != 1:
        raise ValueError("expected one upload config")
    offset = data.index(tag)
    if data[offset + 16 : offset + 248] != bytes(232):
        raise ValueError("nonempty config")
    if data.count(blob) != 1 or data.count(COLD_TAG) != 1:
        raise ValueError("expected one return template")
    cold_offset += data.index(blob)
    if cold_offset % 4:
        raise ValueError("unaligned ROM patch slot")
    (out / "upload.dfu").write_bytes(data)
    usb_ids = {
        field.lower() + "_id": int(value, 16)
        for field, value in re.findall(
            r"#define UPLOAD_USB_(VENDOR|PRODUCT)_ID (0x[0-9a-fA-F]+)",
            (out / "usb_identity.h").read_text(),
        )
    }
    if set(usb_ids) != {"vendor_id", "product_id"}:
        raise ValueError("Missing helper USB identity")
    manifest = dict(
        schema=3,
        storage_inspection=True,
        usb={**usb_ids, "winusb": True},
        mode="stream-file",
        rom_sha256=ROM_SHA,
        bytes=len(data),
        sha256=sha(data),
        nonce_offset=data.index(NONCE),
        config_offset=offset,
        cold_init_offset=cold_offset,
    )
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
