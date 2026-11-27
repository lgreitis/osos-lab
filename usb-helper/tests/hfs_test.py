# SPDX-License-Identifier: GPL-3.0-only
"""Uploads and inspection on Mac-formatted disk images, run through the helper's
C implementation and checked with fsck_hfs (macOS only)."""

import os
import platform
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from helper_test import MOCK

HERE = Path(__file__).resolve().parents[1]
READER = HERE.parent / "loader/hfsplus"
BLOCK = 4096

# The disk is an image file; FAT entry points are never reached on HFS+.
HARNESS = r"""
#define TEST_IMPLEMENTATION
#include "mock.h"
#include "upload.h"
#include "sha256.h"
uint8_t test_buffer[65536] __attribute__((aligned(32)));
struct upload_result test_result;
struct storage_result test_storage;
static FILE *disk;
void filesystem_init(void) {}
int disk_mount_all(void) {abort();}
int disk_unmount_all(void) {abort();}
bool disk_partinfo(int n, struct partinfo *p) {(void)n;(void)p;abort();}
unsigned fat_get_bytes_per_sector(void) {abort();}
bool fat_size(sector_t *s, sector_t *f) {(void)s;(void)f;abort();}
unsigned fat_get_cluster_size(void) {abort();}
void ata_sleepnow(void) {}
bool file_exists(const char *p) {(void)p;abort();}
bool dir_exists(const char *p) {(void)p;abort();}
int test_mkdir(const char *p) {(void)p;abort();}
int test_open(const char *p,int f,...) {(void)p;(void)f;abort();}
int test_close(int fd) {(void)fd;abort();}
ssize_t test_read(int fd,void *p,size_t n) {(void)fd;(void)p;(void)n;abort();}
ssize_t test_write(int fd,const void *p,size_t n) {(void)fd;(void)p;(void)n;abort();}
int test_fsync(int fd) {(void)fd;abort();}
int test_rename(const char *a,const char *b) {(void)a;(void)b;abort();}
int test_remove(const char *p) {(void)p;abort();}
int test_rmdir(const char *p) {(void)p;abort();}
DIR *test_opendir(const char *p) {(void)p;abort();}
struct dirinfo dir_get_info(DIR *d,struct dirent *e) {(void)d;(void)e;abort();}
off_t filesize(int fd) {(void)fd;abort();}
/* HFS_TEST_CUT=n stops the process before the nth write reaches the disk, as
 * a power cut would; HFS_TEST_COUNT prints the disk traffic on exit. */
static long reads, writes, cut_at;
static uint64_t read_bytes, written_bytes;
int ata_read_sectors(uint64_t s, int n, void *b)
{
    ++reads;
    read_bytes += (uint64_t)n * 512;
    return fseeko(disk, (off_t)s * 512, SEEK_SET) || fread(b, 512, n, disk) != (size_t)n;
}
int ata_write_sectors(uint64_t s, int n, const void *b)
{
    assert(upload_write_allowed(s, n));
    if (++writes == cut_at) {
        fflush(disk);
        _exit(3);
    }
    written_bytes += (uint64_t)n * 512;
    return fseeko(disk, (off_t)s * 512, SEEK_SET) || fwrite(b, 512, n, disk) != (size_t)n;
}
int ata_flush(void) {return fflush(disk);}

static uint8_t *load(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseeko(f, 0, SEEK_END);
    *size = ftello(f);
    fseeko(f, 0, SEEK_SET);
    uint8_t *data = malloc(*size + 1);
    assert(fread(data, 1, *size, f) == *size);
    fclose(f);
    return data;
}

static void configure(struct upload_config *c, const char *item, unsigned overwrite,
                      uint8_t **data)
{
    char local[512];
    const char *eq = strchr(item, '=');
    assert(eq && (size_t)(eq - item) < sizeof(local));
    memcpy(local, item, eq - item);
    local[eq - item] = 0;
    memset(c, 0, sizeof(*c));
    memcpy(c->tag, "REPRISE-UPLOAD2", 15);
    *data = load(local, &c->size);
    c->overwrite = overwrite;
    strcpy(c->path, eq + 1);
    struct sha256 h;
    sha256_init(&h);
    sha256_update(&h, *data, c->size);
    sha256_final(&h, c->sha256);
}

static void report_traffic(void)
{
    if (getenv("HFS_TEST_COUNT"))
        fprintf(stderr, "writes=%ld reads=%ld read_bytes=%llu written_bytes=%llu\n",
                writes, reads, (unsigned long long)read_bytes,
                (unsigned long long)written_bytes);
}

/* hfs-test IMAGE install|keep|inspect [LOCAL=/ipod/path ...]
 * Prints one result line per request and one for the end of the session when
 * that changes the result. */
int main(int argc, char **argv)
{
    assert(argc >= 3 && (disk = fopen(argv[1], "r+b")));
    atexit(report_traffic);
    if (getenv("HFS_TEST_CUT"))
        cut_at = atol(getenv("HFS_TEST_CUT"));
    fseeko(disk, 0, SEEK_END);
    uint64_t sectors = ftello(disk) / 512;
    const char *mode = argv[2];
    unsigned overwrite = !strcmp(mode, "inspect") ? 2 : !strcmp(mode, "keep") ? 0 : 1;
    uint8_t *data = NULL;
    struct upload_config c;
    if (argc > 3) {
        configure(&c, argv[3], overwrite, &data);
    } else {
        memset(&c, 0, sizeof(c));
        c.overwrite = overwrite;
        strcpy(c.path, "/Reprise/x");
    }
    memcpy(&upload_config, &c, sizeof(c));
    memset(test_result.nonce, 7, 16);
    upload_prepare_hfs(sectors);
    if (overwrite >= 2 || test_result.rc) {
        assert(test_result.state == 4);
        printf("rc=%d existing=%u free=%llu\n", test_result.rc,
               test_storage.existing_files, (unsigned long long)test_storage.free_bytes);
        upload_close();
        return 0;
    }
    for (int i = 3; i < argc; ++i) {
        if (i > 3) {
            free(data);
            configure(&c, argv[i], overwrite, &data);
            upload_next(&c);
        }
        if (test_result.state == 1) {
            test_result.state = 2;
            upload_start();
            for (uint32_t off = 0; off < c.size && test_result.state == 2;) {
                unsigned n = MIN(c.size - off, 65536);
                memcpy(test_buffer, data + off, n);
                test_result.received += n;
                upload_write(n);
                off += n;
            }
            while (test_result.state == 3)
                upload_verify_step();
        }
        printf("rc=%d %s\n", test_result.rc, c.path);
        if (test_result.rc)
            break;
        assert(test_result.state == 4 && !test_result.rejected);
        assert(!memcmp(test_result.disk_sha256, c.sha256, 32));
    }
    int open_rc = test_result.rc;
    upload_close();
    if (test_result.rc != open_rc)
        printf("rc=%d close\n", test_result.rc);
    return 0;
}
"""


def run(*command, **options):
    return subprocess.run(
        list(map(str, command)), check=True, capture_output=True, text=True, **options
    ).stdout


def volume(root, files):
    """A clean journaled HFS+ volume made by macOS, as raw bytes."""
    image, mount = root / "volume.dmg", root / "mount"
    mount.mkdir()
    run(
        "hdiutil",
        "create",
        "-size",
        "16m",
        "-layout",
        "SPUD",
        "-fs",
        "HFS+J",
        "-volname",
        "IPOD",
        "-o",
        image,
    )
    run("hdiutil", "attach", "-nobrowse", "-mountpoint", mount, image)
    try:
        for path, data in files.items():
            target = mount / path.lstrip("/")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    finally:
        run("hdiutil", "detach", mount)
    run("hdiutil", "convert", image, "-format", "UDTO", "-o", root / "volume")
    disk = (root / "volume.cdr").read_bytes()
    for i in range(1, 16):
        entry = disk[i * 512 : (i + 1) * 512]
        if entry[48:57] == b"Apple_HFS":
            start, count = struct.unpack_from(">II", entry, 8)
            return disk[start * 512 : (start + count) * 512]
    raise AssertionError("no Apple_HFS partition")


def classic_disk(volume):
    """An iPod classic disk: MBR in 4 KiB units with the firmware partition
    first, then a 4 KiB-block partition map holding one Apple_HFS volume."""
    firmware_blocks, map_blocks = 64, 63
    volume_blocks = -(-len(volume) // BLOCK)
    data_blocks = 1 + map_blocks + volume_blocks
    disk = bytearray((8 + firmware_blocks + data_blocks) * BLOCK)
    for i, (kind, start, count) in enumerate(
        [(0, 8, firmware_blocks), (0xAF, 8 + firmware_blocks, data_blocks)]
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
    return disk


class MountedVolume:
    """The HFS+ volume of a disk image, attached read-only after fsck_hfs."""

    def __init__(self, test, disk):
        self.test, self.disk = test, disk

    def __enter__(self):
        data = self.disk.read_bytes()
        offset = data.index(b"H+\x00\x04") - 1024
        block, blocks = struct.unpack_from(">II", data, offset + 1024 + 40)
        self.raw = self.disk.with_suffix(".volume")
        self.raw.write_bytes(data[offset : offset + block * blocks])
        out = run(
            "hdiutil",
            "attach",
            "-nomount",
            "-readonly",
            "-imagekey",
            "diskimage-class=CRawDiskImage",
            self.raw,
        )
        self.device = out.split()[0]
        check = subprocess.run(
            ["fsck_hfs", "-fn", self.device], capture_output=True, text=True
        )
        self.test.assertEqual(check.returncode, 0, check.stdout + check.stderr)
        self.mount = self.disk.with_suffix(".mount")
        self.mount.mkdir()
        run("diskutil", "mount", "readOnly", "-mountPoint", self.mount, self.device)
        return self.mount

    def __exit__(self, *exc):
        run("hdiutil", "detach", self.device)
        self.raw.unlink()
        self.mount.rmdir()


class RawVolume:
    """The HFS+ volume of a disk image, attached without mounting; repairs are
    written back into the disk image on exit."""

    def __init__(self, disk):
        self.disk = disk

    def __enter__(self):
        data = self.disk.read_bytes()
        self.offset = data.index(b"H+\x00\x04") - 1024
        block, blocks = struct.unpack_from(">II", data, self.offset + 1024 + 40)
        self.raw = self.disk.with_suffix(".volume")
        self.raw.write_bytes(data[self.offset : self.offset + block * blocks])
        out = run(
            "hdiutil",
            "attach",
            "-nomount",
            "-imagekey",
            "diskimage-class=CRawDiskImage",
            self.raw,
        )
        self.device = out.split()[0]
        return self

    def check(self):
        check = subprocess.run(["fsck_hfs", "-fn", self.device], capture_output=True)
        return check.returncode == 0

    def repair(self):
        subprocess.run(["fsck_hfs", "-fy", self.device], capture_output=True)

    def size(self, path):
        mount = self.disk.with_suffix(".mount")
        mount.mkdir()
        try:
            run("diskutil", "mount", "readOnly", "-mountPoint", mount, self.device)
            try:
                target = mount / path.lstrip("/")
                return target.stat().st_size if target.exists() else None
            finally:
                run("diskutil", "unmount", mount)
        finally:
            mount.rmdir()

    def __exit__(self, *exc):
        run("hdiutil", "detach", self.device)
        data = bytearray(self.disk.read_bytes())
        volume = self.raw.read_bytes()
        data[self.offset : self.offset + len(volume)] = volume
        self.disk.write_bytes(data)
        self.raw.unlink()


@unittest.skipUnless(
    platform.system() == "Darwin"
    and shutil.which("hdiutil")
    and shutil.which("fsck_hfs"),
    "needs macOS disk image tools",
)
class HfsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = p = Path(cls.tmp.name)
        (p / "mock.h").write_text(MOCK)
        for name in [
            "config.h",
            "system.h",
            "file.h",
            "dir.h",
            "file_internal.h",
            "disk.h",
            "fat.h",
            "ata.h",
        ]:
            (p / name).write_text('#include "mock.h"\n')
        (p / "test_memory.h").write_text("""
#define UPLOAD_CONFIG_QUALIFIER
extern uint8_t test_buffer[65536] __attribute__((aligned(32)));
extern struct upload_result test_result;
extern struct storage_result test_storage;
#define UPLOAD_DATA test_buffer
#define UPLOAD_RESULT (&test_result)
#define STORAGE_RESULT (&test_storage)
""")
        (p / "harness.c").write_text(HARNESS)
        run(
            "cc",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{p}",
            f"-I{HERE}",
            f"-I{READER}",
            "-DUPLOAD_TEST_MEMORY",
            p / "harness.c",
            HERE / "file.c",
            HERE / "sha256.c",
            HERE / "hfs-upload.c",
            READER / "reader.c",
            "-o",
            p / "hfs-test",
        )
        library = {
            f"/iPod_Control/Music/F{i:02d}/T{j}.m4a": os.urandom(3000)
            for i in range(4)
            for j in range(40)
        }
        library["/notes.txt"] = b"user file"
        (p / "library").mkdir()
        cls.library = library
        cls.base = classic_disk(volume(p / "library", library))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.work = Path(tempfile.mkdtemp(dir=self.root))
        self.disk = self.work / "disk.img"
        self.disk.write_bytes(self.base)

    def items(self, files):
        items = []
        for i, (path, data) in enumerate(files):
            local = self.work / f"upload-{i}"
            local.write_bytes(data)
            items.append(f"{local}={path}")
        return items

    def helper(self, mode, files=()):
        return run(
            self.root / "hfs-test", self.disk, mode, *self.items(files)
        ).splitlines()

    def install(self):
        files = [
            ("/Reprise/Firmware/loader.bin", os.urandom(129025)),
            ("/Reprise/Firmware/reprise.bin", os.urandom(3 * 1024 * 1024 + 7)),
            ("/Reprise/Extensions/eq.rvx", os.urandom(40000)),
            ("/Reprise/Extensions/fx/dsp.rvx", os.urandom(70000)),
            ("/Reprise/Extensions/fx/empty.bin", b""),
        ]
        self.assertEqual(
            self.helper("install", files), [f"rc=0 {path}" for path, _ in files]
        )
        return dict(files)

    def test_upload_creates_folders_and_reads_back(self):
        files = self.install()
        with MountedVolume(self, self.disk) as mount:
            for path, data in files.items():
                self.assertEqual((mount / path.lstrip("/")).read_bytes(), data, path)
            for path, data in self.library.items():
                self.assertEqual((mount / path.lstrip("/")).read_bytes(), data, path)

    def test_replace_keep_and_inspect(self):
        self.install()
        new = os.urandom(5000)
        self.assertEqual(
            self.helper("install", [("/Reprise/Firmware/reprise.bin", new)]),
            ["rc=0 /Reprise/Firmware/reprise.bin"],
        )
        self.assertEqual(
            self.helper("keep", [("/Reprise/Extensions/eq.rvx", b"x")]),
            ["rc=-304 existing=0 free=0"],
        )
        result = self.helper("inspect", [("/Reprise/Firmware/reprise.bin", b"x" * 100)])
        self.assertRegex(result[0], r"^rc=0 existing=3 free=\d+$")
        with MountedVolume(self, self.disk) as mount:
            self.assertEqual((mount / "Reprise/Firmware/reprise.bin").read_bytes(), new)
        self.assertEqual(
            self.helper("inspect", [("/Reprise/Firmware/reprise.bin", b"x" * 10**9)])[
                0
            ][:9],
            "rc=-322 e",
        )

    def test_volume_not_cleanly_unmounted_is_refused(self):
        disk = bytearray(self.base)
        header = disk.index(b"H+\x00\x04")
        disk[header + 6] &= ~1
        self.disk.write_bytes(disk)
        self.assertEqual(
            self.helper("install", [("/Reprise/a.bin", b"a")]),
            ["rc=-326 existing=0 free=0"],
        )
        self.assertEqual(self.disk.read_bytes(), disk)

    def test_power_cut_at_any_write_leaves_the_old_or_the_new_file(self):
        """macOS mounts the state the primary volume header describes; where
        fsck_hfs asks for a repair, the repair keeps that state, and the helper
        accepts the volume again."""
        self.install()
        installed = self.disk.read_bytes()
        old = 3 * 1024 * 1024 + 7
        new = os.urandom(70000)
        local = self.work / "new"
        local.write_bytes(new)
        item = f"{local}=/Reprise/Firmware/reprise.bin"
        counted = subprocess.run(
            [self.root / "hfs-test", self.disk, "install", item],
            check=True,
            capture_output=True,
            text=True,
            env=dict(os.environ, HFS_TEST_COUNT="1"),
        )
        writes = int(counted.stderr.split()[0].split("=")[1])
        seen = set()
        for cut in range(1, writes + 1):
            self.disk.write_bytes(installed)
            rc = subprocess.run(
                [self.root / "hfs-test", self.disk, "install", item],
                capture_output=True,
                env=dict(os.environ, HFS_TEST_CUT=str(cut)),
            ).returncode
            self.assertEqual(rc, 3, cut)
            with RawVolume(self.disk) as volume:
                size = volume.size("/Reprise/Firmware/reprise.bin")
                self.assertIn(size, (len(new), old), cut)
                if not volume.check():
                    volume.repair()
                    self.assertTrue(volume.check(), cut)
                    self.assertEqual(volume.size("/Reprise/Firmware/reprise.bin"), size)
            seen.add(size == len(new))
            self.assertRegex(self.helper("inspect")[0], r"^rc=0 existing=3 ", cut)
        self.assertEqual(seen, {False, True})

    def test_reused_catalog_node_ids_are_refused(self):
        disk = bytearray(self.base)
        header = disk.index(b"H+\x00\x04")
        disk[header + 6] |= 0x10
        self.disk.write_bytes(disk)
        self.assertEqual(
            self.helper("install", [("/Reprise/a.bin", b"a")]),
            ["rc=-324 existing=0 free=0"],
        )
        self.assertEqual(self.disk.read_bytes(), disk)

    def test_disk_without_hfs_is_refused(self):
        self.disk.write_bytes(bytes(len(self.base)))
        self.assertEqual(
            self.helper("install", [("/Reprise/a.bin", b"a")]),
            ["rc=-324 existing=0 free=0"],
        )
