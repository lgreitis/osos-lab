"""Exercise the ARM upload helper with a private copy of a disk image."""

import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VM = ROOT.parent / "reprisevm"
sys.path.insert(0, str(VM / "tools/ipodvm"))
from core import GDB  # noqa: E402

RESULT = 0x2201FA80
BUFFER = 0x09800000
CONFIG = struct.Struct("<16sII32s192s")


def config(path, data, overwrite):
    return CONFIG.pack(
        b"REPRISE-UPLOAD2",
        len(data),
        overwrite,
        hashlib.sha256(data).digest(),
        path.encode(),
    )


def check(gdb, state):
    record = gdb.read(RESULT, 320)
    actual, rc = struct.unpack_from("<Ii", record, 24)
    if actual != state or rc:
        raise RuntimeError(f"helper state={actual}, rc={rc} (expected state {state})")
    return record


def stop_before_primary_header(g, symbols):
    if not g.read(symbols["hfs"], 1)[0]:
        raise ValueError("the volume-header interruption requires HFS+")
    primary = int.from_bytes(g.read(symbols["data_start"], 8), "little") + 2
    point = (1, symbols["ata_write_sectors"], 4)
    # The Classic helper is ARM code. Enter the last verify step, which commits
    # the file, then stop before the primary sector reaches the ATA driver.
    g.set_register(14, g.registers()["pc"])
    g.set_register(15, symbols["upload_verify_step"])
    g.insert(point)
    while True:
        g.send("c")
        if not g.wait_stop(30) or g.registers()["pc"] != point[1]:
            raise RuntimeError("helper did not reach the primary header write")
        if g.register(0) == primary:
            print(f"interrupted before primary HFS+ header at sector {primary}")
            return
        g.remove(point)
        g.step()
        g.insert(point)


def replay(elf, disk, nor, uploads, inspect=False, interrupt=False, header_cut=False):
    nm = ROOT / "toolchains/rockbox-arm/bin/arm-elf-eabi-nm"
    symbols = {
        v[2]: int(v[0], 16)
        for line in subprocess.check_output([nm, elf], text=True).splitlines()
        if len(v := line.split()) == 3
    }
    with tempfile.TemporaryDirectory(dir="/tmp", prefix="writer-") as tmp:
        run = Path(tmp)
        cmd = [
            VM / "build/qemu-system-arm",
            "-M",
            "iPod-Classic",
            "-kernel",
            elf,
            "-drive",
            f"if=ide,format=raw,file={str(disk).replace(',', ',,')}",
            "-display",
            "none",
            "-serial",
            "none",
            "-monitor",
            "none",
            "-S",
            "-gdb",
            f"unix:{run}/gdb.sock,server=on,wait=off",
        ]
        env = dict(os.environ, IPOD_NOR_IMAGE=str(nor), PMUCHARGER="adapter")
        with subprocess.Popen(cmd, env=env, stderr=subprocess.DEVNULL) as vm:
            try:
                for _ in range(100):
                    if (run / "gdb.sock").exists():
                        break
                    time.sleep(0.05)
                with GDB(run) as g:

                    def stop_at(name):
                        point = (1, symbols[name], 4)
                        g.insert(point)
                        g.send("c")
                        if not g.wait_stop(30):
                            g.interrupt()
                            raise RuntimeError(
                                f"waiting for {name}, pc={g.registers()['pc']:#x}"
                            )
                        g.remove(point)

                    stop_at("main")
                    path, data = uploads[0]
                    g.write(
                        symbols["upload_config"],
                        config(path, data, 2 if inspect else 1),
                    )
                    stop_at("upload_usb")
                    if inspect:
                        check(g, 4)
                        print(
                            "inspection:",
                            struct.unpack("<QQQII", g.read(0x2201FBC0, 32)),
                        )
                        return
                    for index, (path, data) in enumerate(uploads):
                        if index:
                            g.write(BUFFER, config(path, data, 1))
                            g.call(symbols["upload_next"], BUFFER)
                        check(g, 1)
                        g.write(RESULT + 24, struct.pack("<I", 2))
                        g.call(symbols["upload_start"])
                        for off in range(0, len(data), 65536):
                            chunk = data[off : off + 65536]
                            g.write(BUFFER, chunk)
                            g.write(RESULT + 36, struct.pack("<I", off + len(chunk)))
                            g.call(symbols["upload_write"], len(chunk))
                            if interrupt and off == 0:
                                check(g, 2)
                                return
                        check(g, 3)
                        for _ in range((len(data) + 65535) // 65536):
                            g.call(symbols["upload_verify_step"])
                        if header_cut:
                            stop_before_primary_header(g, symbols)
                            return
                        g.call(symbols["upload_verify_step"])
                        result = check(g, 4)
                        digest = hashlib.sha256(data).digest()
                        if result[64:128] != digest * 2:
                            raise RuntimeError("source or disk digest mismatch")
                        print(f"{path}: {len(data)} bytes verified", flush=True)
                    g.call(symbols["upload_close"])
                    check(g, 4)
            finally:
                vm.terminate()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("disk", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("files", nargs="+", help="LOCAL_FILE=/absolute/ipod/path")
    parser.add_argument("--nor", type=Path, default=ROOT / "inputs/nor.bin")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--inspect", action="store_true")
    mode.add_argument(
        "--interrupt", action="store_true", help="cut during a multi-chunk transfer"
    )
    mode.add_argument(
        "--interrupt-before-header",
        action="store_true",
        help="cut between alternate and primary HFS+ header writes",
    )
    a = parser.parse_args()
    with a.disk.open("rb") as source, a.output.open("xb") as target:
        shutil.copyfileobj(source, target)
    items = []
    for arg in a.files:
        local, path = arg.split("=", 1)
        items.append((path, Path(local).read_bytes()))
    replay(
        a.elf, a.output, a.nor, items, a.inspect, a.interrupt, a.interrupt_before_header
    )
