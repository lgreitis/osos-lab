# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the patched bootloader's file reads against host files and disk images."""

import platform
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Stand-ins for the Rockbox headers cfw-file-ipod6g.c includes. Staging goes to
# a host buffer, FAT files are host files below the working directory, and the
# disk is an image file read through storage_read_sectors.
SHIM = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <setjmp.h>
#include <assert.h>
#define CFW_STAGE_START 0u
#define CFW_COMPANION_START 0xc00000u
#define CFW_STAGE_END 0x1000000u
#define CFW_BOOTSTRAP_BYTES 0x1f800u
#define CACHEALIGN_ATTR
#define HZ 100
#define IF_MD(...)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
static unsigned char staging[CFW_STAGE_END], iram[CFW_BOOTSTRAP_BYTES];
#define S5L8702_UNCACHED_ADDR(p) (staging + (uintptr_t)(p))
#define IRAM0_ORIG iram
static jmp_buf stopped;
static const char *error;
static int handed_off;
#define printf(...) ((void)0)
#define sleep(t) (error = message, longjmp(stopped, 1))
static void lcd_shutdown(void) {}
static void disable_irq(void) {}
static void disable_fiq(void) {}
static void eint_init(void) {}
static unsigned ATA_IRQ_MASK, ATA_IRQ;
#define commit_discard_idcache() (handed_off = 1, longjmp(stopped, 1))
static off_t filesize(int fd) { struct stat s; assert(!fstat(fd, &s)); return s.st_size; }
static int host_open(const char *p, int flags) { return open(p + 1, flags); }
#define open host_open
struct storage_info { unsigned int sector_size; uint64_t num_sectors; };
static FILE *disk;
static unsigned sector_size;
static void storage_get_info(int drive, struct storage_info *info)
{
    (void)drive;
    fseeko(disk, 0, SEEK_END);
    info->sector_size = sector_size;
    info->num_sectors = ftello(disk) / sector_size;
}
static int storage_read_sectors(uint64_t start, int count, void *buf)
{
    return fseeko(disk, (off_t)(start * sector_size), SEEK_SET) ||
           fread(buf, sector_size, count, disk) != (size_t)count;
}
"""

MAIN = r"""
/* argv: expected result, then "fat" or an image path and its sector size.
 * Results: "ok", "no volume" (cfw_run_file returned) or the stop message. */
int main(int argc, char **argv)
{
    assert(argc >= 3);
    bool hfs = strcmp(argv[2], "fat");
    if (hfs) {
        assert(argc == 4 && (disk = fopen(argv[2], "rb")));
        sector_size = atoi(argv[3]);
    }
    if (!setjmp(stopped)) {
        cfw_run_file(hfs);
        return strcmp(argv[1], "no volume") ? 2 : 0;
    }
    if (!handed_off)
        return strcmp(error, argv[1]) ? 3 : 0;
    FILE *f = fopen("expected", "rb");
    assert(f);
    static unsigned char want[CFW_STAGE_END];
    size_t firmware = fread(want, 1, CFW_COMPANION_START, f);
    assert(!memcmp(staging, want, firmware));
    fclose(f);
    assert(!memcmp(iram, staging + CFW_COMPANION_START, CFW_BOOTSTRAP_BYTES));
    return strcmp(argv[1], "ok") ? 4 : 0;
}
"""

FIRMWARE, LOADER = 32769, 0x1F800
BLOCK = 4096


def boot_source():
    patch = (ROOT / "patches/rockbox/0001-cfw-file-boot.patch").read_text()
    section = patch.split("+++ b/bootloader/cfw-file-ipod6g.c\n", 1)[1]
    section = section.split("diff --git", 1)[0]
    lines = [line[1:] for line in section.splitlines() if line.startswith("+")]
    return "\n".join(line for line in lines if not line.startswith('#include "'))


def build(root):
    reader = (ROOT / "loader/hfsplus/reader.c").read_text()
    reader = reader.replace('#include "reader.h"', "")
    header = (ROOT / "loader/hfsplus/reader.h").read_text()
    code, binary = root / "boot.c", root / "boot-test"
    code.write_text(SHIM + header + reader + boot_source() + MAIN)
    subprocess.run(
        ["cc", "-Wno-int-to-pointer-cast", str(code), "-o", str(binary)], check=True
    )
    return binary


def contents(firmware_size, loader_size):
    firmware = bytes(i * 7 & 255 for i in range(firmware_size))
    loader = bytes(i * 13 & 255 for i in range(loader_size))
    return firmware, loader


def hfs_volume(root, files):
    """A journaled HFS+ volume holding files, as raw bytes (macOS only)."""
    image, mount = root / "volume.dmg", root / "mount"
    mount.mkdir()
    subprocess.run(
        ["hdiutil", "create", "-size", "4m", "-layout", "SPUD", "-fs", "HFS+J"]
        + ["-volname", "IPOD", "-o", str(image)],
        check=True,
        capture_output=True,
    )
    subprocess.run(
        ["hdiutil", "attach", "-nobrowse", "-mountpoint", str(mount), str(image)],
        check=True,
        capture_output=True,
    )
    try:
        for i in range(300):
            (mount / f"filler-{i}.txt").write_text(str(i))
        for path, data in files.items():
            target = mount / path.lstrip("/")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    finally:
        subprocess.run(
            ["hdiutil", "detach", str(mount)], check=True, capture_output=True
        )
    raw = root / "volume"
    subprocess.run(
        ["hdiutil", "convert", str(image), "-format", "UDTO", "-o", str(raw)],
        check=True,
        capture_output=True,
    )
    disk = (root / "volume.cdr").read_bytes()
    for i in range(1, 16):
        entry = disk[i * 512 : (i + 1) * 512]
        if entry[48:57] == b"Apple_HFS":
            start, count = struct.unpack_from(">II", entry, 8)
            return disk, disk[start * 512 : (start + count) * 512]
    raise AssertionError("no Apple_HFS partition")


def unclean(disk):
    """The disk with the volume's kHFSVolumeUnmountedBit cleared."""
    disk = bytearray(disk)
    header = disk.index(b"H+\x00\x04")
    disk[header + 6] &= ~1
    return bytes(disk)


def classic_disk(volume, firmware_type=0):
    """An iPod classic disk: MBR in 4 KiB units with the firmware partition
    first, then a 4 KiB-block partition map holding one Apple_HFS volume."""
    firmware_blocks, map_blocks = 64, 63
    volume_blocks = -(-len(volume) // BLOCK)
    data_blocks = 1 + map_blocks + volume_blocks
    disk = bytearray((8 + firmware_blocks + data_blocks) * BLOCK)
    for i, (kind, start, count) in enumerate(
        [(firmware_type, 8, firmware_blocks), (0xAF, 8 + firmware_blocks, data_blocks)]
    ):
        struct.pack_into("<4xB3xII", disk, 446 + 16 * i, kind, start, count)
    disk[510:512] = b"\x55\xaa"
    base = (8 + firmware_blocks) * BLOCK
    disk[base : base + 8] = b"ER" + struct.pack(">HI", BLOCK, data_blocks)
    for i, (start, count, kind) in enumerate(
        [
            (1, map_blocks, b"Apple_partition_map"),
            (1 + map_blocks, volume_blocks, b"Apple_HFS"),
        ],
        1,
    ):
        entry = base + i * BLOCK
        disk[entry : entry + 2] = b"PM"
        struct.pack_into(">III", disk, entry + 4, 2, start, count)
        disk[entry + 48 : entry + 48 + len(kind)] = kind
    start = base + (1 + map_blocks) * BLOCK
    disk[start : start + len(volume)] = volume
    return bytes(disk)


class BootFilesTests(unittest.TestCase):
    def run_boot(self, binary, root, expected, *disk):
        result = subprocess.run(
            [str(binary), expected, *map(str, disk)], cwd=root, capture_output=True
        )
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def expect(self, root, firmware, loader):
        (root / "expected").write_bytes(
            firmware + bytes(max(0, 0xC00000 - len(firmware))) + loader
        )

    def test_fat_nested_paths_and_missing_or_invalid_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = build(root)
            folder = root / "Reprise/Firmware"
            folder.mkdir(parents=True)
            cases = [
                (None, None, "File open failed"),
                (FIRMWARE, None, "File open failed"),
                (0, LOADER, "File size out of bounds"),
                (0xC00001, LOADER, "File size out of bounds"),
                (FIRMWARE, LOADER - 1, "File size out of bounds"),
                (FIRMWARE, LOADER, "ok"),
            ]
            for firmware_size, loader_size, expected in cases:
                with self.subTest(firmware=firmware_size, loader=loader_size):
                    firmware, loader = contents(firmware_size or 0, loader_size or 0)
                    for name, size, data in (
                        ("reprise.bin", firmware_size, firmware),
                        ("loader.bin", loader_size, loader),
                    ):
                        (folder / name).unlink(missing_ok=True)
                        if size is not None:
                            (folder / name).write_bytes(data)
                    self.expect(root, firmware, loader)
                    self.run_boot(binary, root, expected, "fat")

    @unittest.skipUnless(
        platform.system() == "Darwin" and shutil.which("hdiutil"), "needs hdiutil"
    )
    def test_hfs_layouts_sector_sizes_and_missing_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = build(root)
            firmware, loader = contents(FIRMWARE * 9, LOADER + 5)
            self.expect(root, firmware, loader)
            files = {
                "/Reprise/Firmware/reprise.bin": firmware,
                "/reprise/firmware/LOADER.BIN": loader,
            }
            (root / "full").mkdir()
            (root / "partial").mkdir()
            apm, volume = hfs_volume(root / "full", files)
            _, partial = hfs_volume(
                root / "partial", {"/Reprise/Firmware/reprise.bin": firmware}
            )
            images = {
                "apm.img": apm,
                "classic.img": classic_disk(volume),
                "classic-3f.img": classic_disk(volume, 0x3F),
                "partial.img": classic_disk(partial),
                "unclean.img": unclean(classic_disk(volume)),
                "blank.img": bytes(1 << 20),
            }
            for name, data in images.items():
                (root / name).write_bytes(data)
            cases = [
                ("apm.img", 512, "ok"),
                ("classic.img", 512, "ok"),
                ("classic.img", 4096, "ok"),
                ("classic-3f.img", 4096, "ok"),
                ("partial.img", 4096, "File open failed"),
                ("unclean.img", 4096, "ok"),
                ("blank.img", 4096, "no volume"),
                ("classic.img", 2048, "no volume"),
            ]
            for name, sector, expected in cases:
                with self.subTest(image=name, sector=sector):
                    self.run_boot(binary, root, expected, root / name, sector)
