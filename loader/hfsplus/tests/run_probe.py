"""From osos-lab, run python3.11 loader/hfsplus/tests/run_probe.py DISK.img."""

import argparse
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
VM = ROOT.parent / "reprisevm"
sys.path.insert(0, str(VM / "tools/ipodvm"))
from core import GDB, QMP  # noqa: E402


def verify(disk):
    reader = ROOT / "loader/hfsplus"
    prefix = str(ROOT / "toolchains/rockbox-arm/bin/arm-elf-eabi-")
    with tempfile.TemporaryDirectory(prefix="hfs-probe-", dir="/tmp") as tmp:
        run = Path(tmp)
        elf = run / "probe.elf"
        subprocess.run(
            [
                prefix + "gcc",
                "-mcpu=arm926ej-s",
                "-Os",
                "-ffreestanding",
                "-I" + str(ROOT / "vendor/rockbox/firmware/include"),
                "-I" + str(ROOT / "vendor/rockbox/firmware/libc/include"),
                "-I" + str(reader),
                "-DDISK_BYTES=" + str(disk.stat().st_size),
                "-nostdlib",
                "-Wl,-Ttext=0x08000000,-e,entry",
                reader / "reader.c",
                reader / "tests/ata_probe.c",
                ROOT / ".build/rockbox/firmware/libfirmware.a",
                "-lgcc",
                "-o",
                elf,
            ],
            check=True,
        )
        symbols = {}
        for line in subprocess.check_output(
            [prefix + "nm", elf], text=True
        ).splitlines():
            fields = line.split()
            if len(fields) == 3:
                symbols[fields[2]] = int(fields[0], 16)
        with subprocess.Popen(
            [
                VM / "build/qemu-system-arm",
                "-M",
                "iPod-Classic",
                "-kernel",
                elf,
                "-drive",
                "if=ide,format=raw,file=" + str(disk).replace(",", ",,"),
                "-snapshot",
                "-S",
                "-display",
                "none",
                "-serial",
                "none",
                "-monitor",
                "none",
                "-qmp",
                f"unix:{run}/qmp.sock,server=on,wait=off",
                "-gdb",
                f"unix:{run}/gdb.sock,server=on,wait=off",
            ]
        ) as vm:
            try:
                for _ in range(100):
                    if (run / "gdb.sock").exists() or vm.poll() is not None:
                        break
                    time.sleep(0.05)
                with GDB(run) as gdb:
                    gdb.insert((1, symbols["done"], 4))
                    gdb.send("c")
                    if (
                        not gdb.wait_stop(90)
                        or gdb.registers()["pc"] != symbols["done"]
                    ):
                        raise RuntimeError("ATA probe did not finish")
                    results = struct.unpack("<4I", gdb.read(symbols["result"], 16))
                with QMP(run) as qmp:
                    for i, (name, addr) in enumerate(
                        [("reprise.bin", 0x0B000000), ("loader.bin", 0x0BC00000)]
                    ):
                        expected = (ROOT / "build" / name).read_bytes()
                        rc, size = results[2 * i : 2 * i + 2]
                        if rc or size != len(expected):
                            raise RuntimeError(f"{name}: read result {rc}, size {size}")
                        dump = run / name
                        qmp.execute(
                            "pmemsave", dict(val=addr, size=size, filename=str(dump))
                        )
                        if dump.read_bytes() != expected:
                            raise RuntimeError(f"{name}: staged bytes differ")
                        print(f"{name}: {size:,} staged bytes verified")
            finally:
                vm.terminate()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("disk", type=Path)
    verify(parser.parse_args().disk.resolve())
