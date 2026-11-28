# SPDX-License-Identifier: GPL-3.0-only
"""Uploads, inspection and removal on Mac-formatted disk images, run through the
helper's C implementation and checked with fsck_hfs (macOS only)."""

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
#include "hfs-upload.h"
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
static bool direct; /* hfs_upload_remove called around file.c */
int ata_read_sectors(uint64_t s, int n, void *b)
{
    ++reads;
    read_bytes += (uint64_t)n * 512;
    return fseeko(disk, (off_t)s * 512, SEEK_SET) || fread(b, 512, n, disk) != (size_t)n;
}
int ata_write_sectors(uint64_t s, int n, const void *b)
{
    assert(direct || upload_write_allowed(s, n));
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

/* hfs-test IMAGE install|keep|batch|staged|inspect|remove [LOCAL=/ipod/path ...]
 * batch marks every file but the last with UPLOAD_MORE, staged every file.
 * Prints "none" without an HFS+ volume, else one result line per request and
 * one for the end of the session when that changes the result.
 * hfs-test IMAGE drop ROOT... calls hfs_upload_remove with the roots, a folder
 * when the root ends in a slash, and prints rc and each root's outcome. */
int main(int argc, char **argv)
{
    assert(argc >= 3 && (disk = fopen(argv[1], "r+b")));
    atexit(report_traffic);
    if (getenv("HFS_TEST_CUT"))
        cut_at = atol(getenv("HFS_TEST_CUT"));
    fseeko(disk, 0, SEEK_END);
    uint64_t sectors = ftello(disk) / 512;
    const char *mode = argv[2];
    bool drop = !strcmp(mode, "drop");
    unsigned overwrite = !strcmp(mode, "inspect") || drop ? 2 : !strcmp(mode, "remove") ? 3 :
                         !strcmp(mode, "keep") ? 0 : 1;
    bool staged = !strcmp(mode, "staged"), batch = staged || !strcmp(mode, "batch");
    uint8_t *data = NULL;
    struct upload_config c;
    if (argc > 3 && !drop) {
        configure(&c, argv[3], overwrite, &data);
        if (batch && (staged || argc > 4))
            c.overwrite |= UPLOAD_MORE;
    } else {
        memset(&c, 0, sizeof(c));
        c.overwrite = overwrite;
        strcpy(c.path, "/Reprise/x");
    }
    memcpy(&upload_config, &c, sizeof(c));
    memset(test_result.nonce, 7, 16);
    if (!upload_prepare_hfs(sectors)) {
        puts("none");
        return 0;
    }
    if (drop && !test_result.rc) {
        struct hfs_removal set[HFS_REMOVE_ROOTS + 1];
        char names[HFS_REMOVE_ROOTS + 1][192];
        unsigned n = argc - 3;
        assert(n <= HFS_REMOVE_ROOTS + 1);
        for (unsigned i = 0; i < n; ++i) {
            size_t len = strlen(argv[3 + i]);
            assert(len > 1 && len < sizeof(names[i]));
            memcpy(names[i], argv[3 + i], len + 1);
            set[i].folder = names[i][len - 1] == '/';
            if (set[i].folder)
                names[i][len - 1] = 0;
            set[i].path = names[i];
            set[i].outcome = 9;
        }
        direct = true;
        printf("rc=%d\n", hfs_upload_remove(set, n));
        for (unsigned i = 0; i < n; ++i)
            printf("outcome=%u %s\n", set[i].outcome, argv[3 + i]);
        upload_close();
        return 0;
    }
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
            if (batch && (staged || i < argc - 1))
                c.overwrite |= UPLOAD_MORE;
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


def volume(root, files, attributes=(), locked=()):
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
        for path in attributes:
            run("xattr", "-w", "org.repriseos.test", "1", mount / path.lstrip("/"))
        for path in locked:
            run("chflags", "uchg", mount / path.lstrip("/"))
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


def set_attribute(disk, path):
    """Give one entry of the disk's HFS+ volume an extended attribute through
    macOS, then write the cleanly ejected volume back."""
    data = bytearray(disk.read_bytes())
    offset = data.index(b"H+\x00\x04") - 1024
    block, blocks = struct.unpack_from(">II", data, offset + 1024 + 40)
    raw = disk.with_suffix(".rw")
    raw.write_bytes(data[offset : offset + block * blocks])
    device = run(
        "hdiutil",
        "attach",
        "-nomount",
        "-imagekey",
        "diskimage-class=CRawDiskImage",
        raw,
    ).split()[0]
    mount = disk.with_suffix(".rwmount")
    mount.mkdir()
    try:
        run("diskutil", "mount", "-mountPoint", mount, device)
        run("xattr", "-w", "org.repriseos.test", "1", mount / path.lstrip("/"))
        run("diskutil", "unmount", mount)
    finally:
        run("hdiutil", "detach", device)
    volume = raw.read_bytes()
    data[offset : offset + len(volume)] = volume
    disk.write_bytes(data)
    raw.unlink()
    mount.rmdir()


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


def tree(mount):
    return sorted(
        "/" + str(p.relative_to(mount))
        for p in mount.rglob("*")
        if not any(part.startswith(".") for part in p.relative_to(mount).parts)
    )


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

    def write_count(self):
        data = self.disk.read_bytes()
        return struct.unpack_from(">I", data, data.index(b"H+\x00\x04") + 68)[0]

    def catalog_depth(self):
        data = self.disk.read_bytes()
        header = data.index(b"H+\x00\x04")
        block = struct.unpack_from(">I", data, header + 40)[0]
        start = struct.unpack_from(">I", data, header + 272 + 16)[0]
        return struct.unpack_from(">H", data, header - 1024 + start * block + 14)[0]

    def drop(self, roots):
        return run(self.root / "hfs-test", self.disk, "drop", *roots).splitlines()

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

    def test_removal_keeps_the_library_and_reports_leftovers(self):
        self.install()
        self.assertRegex(
            self.helper("remove")[0], rf"^rc=0 existing={1 | 2 | 4} free=\d+$"
        )
        with MountedVolume(self, self.disk) as mount:
            self.assertEqual(
                tree(mount),
                sorted(
                    {"/iPod_Control", "/iPod_Control/Music", "/notes.txt"}
                    | {f"/iPod_Control/Music/F{i:02d}" for i in range(4)}
                    | set(self.library)
                ),
            )
        self.assertRegex(self.helper("remove")[0], r"^rc=0 existing=0 free=\d+$")

    def test_mac_copied_files_go_and_locked_files_stay(self):
        files = {
            "/Reprise/Firmware/reprise.bin": os.urandom(9000),
            "/Reprise/Themes/flat.rvt": b"t",
            "/Reprise/Themes/locked.rvt": b"l",
            "/Reprise/Data/eq/state.bin": b"s",
            "/notes.txt": b"user file",
        }
        self.disk.write_bytes(
            classic_disk(
                volume(
                    self.work,
                    files,
                    ["/Reprise/Themes/flat.rvt", "/notes.txt"],
                    ["/Reprise/Themes/locked.rvt"],
                )
            )
        )
        result = self.helper("remove")[0]
        self.assertTrue(result.startswith(f"rc=0 existing={2 | 64} "), result)
        with MountedVolume(self, self.disk) as mount:
            self.assertEqual(
                [p for p in tree(mount) if p.startswith("/Reprise")],
                ["/Reprise", "/Reprise/Themes", "/Reprise/Themes/locked.rvt"],
            )
            self.assertEqual(
                run("xattr", "-p", "org.repriseos.test", mount / "notes.txt").strip(),
                "1",
            )

    def test_attributes_of_the_reprise_folder_go_with_it(self):
        # Files written by the helper carry no attributes, so only the
        # folder's own record needs the attributes file rebuilt.
        self.install()
        set_attribute(self.disk, "/Reprise")
        self.assertRegex(self.helper("remove")[0], rf"^rc=0 existing={1 | 2 | 4} ")
        with MountedVolume(self, self.disk) as mount:
            self.assertFalse((mount / "Reprise").exists())

    def test_locked_folders_keep_everything_inside(self):
        files = {
            "/Reprise/Firmware/reprise.bin": b"r",
            "/Reprise/Themes/a.rvt": b"a",
            "/Reprise/Themes/sub/b.rvt": b"b",
            "/Reprise/Data/x.bin": b"x",
        }
        self.disk.write_bytes(
            classic_disk(volume(self.work, files, locked=["/Reprise/Themes"]))
        )
        self.assertTrue(self.helper("remove")[0].startswith(f"rc=0 existing={2 | 64} "))
        with MountedVolume(self, self.disk) as mount:
            self.assertEqual(
                tree(mount),
                [
                    "/Reprise",
                    "/Reprise/Themes",
                    "/Reprise/Themes/a.rvt",
                    "/Reprise/Themes/sub",
                    "/Reprise/Themes/sub/b.rvt",
                ],
            )
        (self.work / "top").mkdir()
        self.disk.write_bytes(
            classic_disk(volume(self.work / "top", files, locked=["/Reprise"]))
        )
        self.assertTrue(
            self.helper("remove")[0].startswith(f"rc=0 existing={32 | 64} ")
        )
        with MountedVolume(self, self.disk) as mount:
            self.assertEqual(len(tree(mount)), 9)

    def test_roots_take_the_folders_above_only_when_empty(self):
        """Several roots, folders and files, go in one rebuild; a folder above
        a root goes with it only when nothing else is left in it."""
        files = {
            "/Reprise/Firmware/reprise.bin": b"r",
            "/Reprise/Extensions/a.rvx": b"a",
            "/Reprise/Data/eq/state.bin": b"s",
            "/Reprise/notes.txt": b"n",
            "/themes/default/theme.bin": b"t",
            "/themes/default/preview.png": b"p",
            "/themes/dark/theme.bin": b"d",
            "/cfw-loader.bin": b"c",
        }
        self.disk.write_bytes(classic_disk(volume(self.work, files)))
        roots = [
            "/Reprise/Extensions/",
            "/Reprise/Data/",
            "/themes/default/theme.bin",
            "/themes/dark/theme.bin",
            "/cfw-loader.bin",
            "/missing/",
            "/Reprise/Extensions/a.rvx",
        ]
        self.assertEqual(
            self.drop(roots),
            ["rc=0"]
            + [f"outcome={o} {r}" for o, r in zip([2, 2, 2, 2, 2, 0, 2], roots)],
        )
        with MountedVolume(self, self.disk) as mount:
            self.assertEqual(
                tree(mount),
                [
                    "/Reprise",
                    "/Reprise/Firmware",
                    "/Reprise/Firmware/reprise.bin",
                    "/Reprise/notes.txt",
                    "/themes",
                    "/themes/default",
                    "/themes/default/preview.png",
                ],
            )
        count = self.write_count()
        self.assertEqual(
            self.drop(["/themes/dark/theme.bin"]),
            ["rc=0", "outcome=0 /themes/dark/theme.bin"],
        )
        self.assertEqual(self.write_count(), count)
        self.assertEqual(
            self.drop(["/themes/default/preview.png", "/Reprise/notes.txt"]),
            [
                "rc=0",
                "outcome=2 /themes/default/preview.png",
                "outcome=2 /Reprise/notes.txt",
            ],
        )
        with MountedVolume(self, self.disk) as mount:
            self.assertEqual(
                tree(mount),
                ["/Reprise", "/Reprise/Firmware", "/Reprise/Firmware/reprise.bin"],
            )

    def test_nested_folder_roots_go_whole(self):
        files = {
            "/a/b/c/x.bin": b"x",
            "/a/b/d.txt": b"d",
            "/a/e.txt": b"e",
            "/keep.txt": b"k",
        }
        for roots in (["/a/b/c/", "/a/"], ["/a/", "/a/b/c/"]):
            self.disk.write_bytes(classic_disk(volume(self.work, files)))
            shutil.rmtree(self.work / "mount")
            for name in ("volume.dmg", "volume.cdr"):
                (self.work / name).unlink()
            self.assertEqual(
                self.drop(roots), ["rc=0"] + [f"outcome=2 {r}" for r in roots]
            )
            with MountedVolume(self, self.disk) as mount:
                self.assertEqual(
                    [p for p in tree(mount) if not p.startswith("/iPod_Control")],
                    ["/keep.txt"],
                )

    def test_locked_folder_above_a_root_keeps_it(self):
        files = {
            "/themes/default/theme.bin": b"t",
            "/themes/dark/theme.bin": b"d",
            "/cfw-loader.bin": b"c",
        }
        self.disk.write_bytes(
            classic_disk(volume(self.work, files, locked=["/themes"]))
        )
        roots = [
            "/themes/default/theme.bin",
            "/themes/dark/theme.bin",
            "/cfw-loader.bin",
        ]
        self.assertEqual(
            self.drop(roots),
            ["rc=0"] + [f"outcome={o} {r}" for o, r in zip([1, 1, 2], roots)],
        )
        with MountedVolume(self, self.disk) as mount:
            self.assertEqual(
                [p for p in tree(mount) if not p.startswith("/iPod_Control")],
                [
                    "/themes",
                    "/themes/dark",
                    "/themes/dark/theme.bin",
                    "/themes/default",
                    "/themes/default/theme.bin",
                ],
            )

    def test_locked_firmware_folder_keeps_both_firmware_files(self):
        files = {
            "/Reprise/Firmware/loader.bin": b"l",
            "/Reprise/Firmware/reprise.bin": b"r",
            "/Reprise/Extensions/a.rvx": b"a",
        }
        self.disk.write_bytes(
            classic_disk(volume(self.work, files, locked=["/Reprise/Firmware"]))
        )
        self.assertTrue(
            self.helper("remove")[0].startswith(f"rc=0 existing={16 | 32 | 64} ")
        )
        with MountedVolume(self, self.disk) as mount:
            self.assertEqual(
                [p for p in tree(mount) if p.startswith("/Reprise")],
                [
                    "/Reprise",
                    "/Reprise/Firmware",
                    "/Reprise/Firmware/loader.bin",
                    "/Reprise/Firmware/reprise.bin",
                ],
            )

    def test_three_level_catalog(self):
        """A catalog with an index level between the root and the leaves is
        copied by an upload and by a removal."""
        library = dict(self.library)
        library.update(
            {f"/iPod_Control/Music/F99/T{i:04d}.m4a": b"" for i in range(3000)}
        )
        self.disk.write_bytes(classic_disk(volume(self.work, library)))
        self.assertEqual(self.catalog_depth(), 3)
        files = self.install()
        self.assertEqual(self.catalog_depth(), 3)
        with MountedVolume(self, self.disk) as mount:
            for path, data in files.items():
                self.assertEqual((mount / path.lstrip("/")).read_bytes(), data, path)
        self.assertRegex(self.helper("remove")[0], rf"^rc=0 existing={1 | 2 | 4} ")
        with MountedVolume(self, self.disk) as mount:
            self.assertEqual(len(tree(mount)), len(library) + 7)

    def test_power_cut_in_a_removal_leaves_all_or_nothing(self):
        self.install()
        installed = self.disk.read_bytes()
        counted = subprocess.run(
            [self.root / "hfs-test", self.disk, "remove"],
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
                [self.root / "hfs-test", self.disk, "remove"],
                capture_output=True,
                env=dict(os.environ, HFS_TEST_CUT=str(cut)),
            ).returncode
            self.assertEqual(rc, 3, cut)
            with RawVolume(self.disk) as volume:
                size = volume.size("/Reprise/Firmware/reprise.bin")
                self.assertIn(size, (3 * 1024 * 1024 + 7, None), cut)
                if not volume.check():
                    volume.repair()
                    self.assertTrue(volume.check(), cut)
                    self.assertEqual(volume.size("/Reprise/Firmware/reprise.bin"), size)
            seen.add(size is None)
            self.assertRegex(
                self.helper("inspect")[0], rf"^rc=0 existing={3 if size else 0} ", cut
            )
        self.assertEqual(seen, {False, True})

    def test_legacy_layout_goes_and_user_files_keep_their_folders(self):
        """The folders and files of releases through 0.4.0: /Revola holds only
        the extension folders, /themes the theme packs, the disk root the
        firmware files. Anything else in those folders keeps them."""
        files = {
            "/Revola/Extensions/a.rvx": b"a",
            "/Revola/Data/eq/state.bin": b"s",
            "/Revola/notes.txt": b"n",
            "/themes/default/theme.bin": b"t",
            "/themes/default/preview.png": b"p",
            "/themes/dark/theme.bin": b"d",
            "/cfw-loader.bin": b"c",
            "/osos-cfw.bin": b"o",
        }
        self.disk.write_bytes(classic_disk(volume(self.work, files)))
        self.assertTrue(
            self.helper("remove")[0].startswith(f"rc=0 existing={1 | 2 | 4} ")
        )
        with MountedVolume(self, self.disk) as mount:
            self.assertEqual(
                [p for p in tree(mount) if not p.startswith("/iPod_Control")],
                [
                    "/Revola",
                    "/Revola/notes.txt",
                    "/themes",
                    "/themes/default",
                    "/themes/default/preview.png",
                ],
            )
        (self.work / "locked").mkdir()
        self.disk.write_bytes(
            classic_disk(
                volume(self.work / "locked", files, locked=["/cfw-loader.bin"])
            )
        )
        self.assertTrue(
            self.helper("remove")[0].startswith(f"rc=0 existing={16 | 2 | 4} ")
        )

    def test_both_layouts_report_each_file_once(self):
        files = {
            "/Reprise/Firmware/loader.bin": b"l",
            "/Reprise/Firmware/reprise.bin": b"r",
            "/cfw-loader.bin": b"c",
            "/osos-cfw.bin": b"o",
        }
        self.disk.write_bytes(classic_disk(volume(self.work, files)))
        self.assertTrue(
            self.helper("remove")[0].startswith(f"rc=0 existing={1 | 2 | 4} ")
        )
        with MountedVolume(self, self.disk) as mount:
            self.assertEqual(
                [p for p in tree(mount) if not p.startswith("/iPod_Control")], []
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

    def test_batch_publishes_once_and_reads_back(self):
        before = self.write_count()
        files = [
            ("/Reprise/Firmware/loader.bin", os.urandom(129025)),
            ("/Reprise/Firmware/reprise.bin", os.urandom(300007)),
            ("/Reprise/Extensions/b.rvx", os.urandom(5000)),
            ("/Reprise/Extensions/A.rvx", os.urandom(4000)),
            ("/Reprise/Games/2048/game.bin", b""),
            ("/Reprise/Games/2048/state/x.bin", b"x"),
            ("/notes-2.txt", b"n"),
        ]
        self.assertEqual(self.helper("batch", files), [f"rc=0 {p}" for p, _ in files])
        self.assertEqual(self.write_count(), before + 1)
        again = [
            ("/Reprise/Firmware/reprise.bin", os.urandom(7000)),
            ("/Reprise/Extensions/c.rvx", b"c"),
            ("/Reprise/Firmware/loader.bin", os.urandom(130000)),
        ]
        self.assertEqual(self.helper("batch", again), [f"rc=0 {p}" for p, _ in again])
        self.assertEqual(self.write_count(), before + 2)
        expected = dict(files) | dict(again) | self.library
        with MountedVolume(self, self.disk) as mount:
            for path, data in expected.items():
                self.assertEqual((mount / path.lstrip("/")).read_bytes(), data, path)

    def test_power_cut_in_a_batch_keeps_all_or_nothing(self):
        self.install()
        installed = self.disk.read_bytes()
        files = [
            ("/Reprise/Firmware/reprise.bin", os.urandom(9000)),
            ("/Reprise/New/a.bin", os.urandom(3000)),
            ("/Reprise/New/b.bin", b"b"),
        ]
        command = [self.root / "hfs-test", self.disk, "batch", *self.items(files)]
        counted = subprocess.run(
            command,
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
                command,
                capture_output=True,
                env=dict(os.environ, HFS_TEST_CUT=str(cut)),
            ).returncode
            self.assertEqual(rc, 3, cut)
            with RawVolume(self.disk) as volume:
                state = [volume.size(path) for path, _ in files]
                self.assertIn(
                    state, ([3 * 1024 * 1024 + 7, None, None], [9000, 3000, 1])
                )
                if not volume.check():
                    volume.repair()
                    self.assertTrue(volume.check(), cut)
                    self.assertEqual([volume.size(path) for path, _ in files], state)
            seen.add(state[0] == 9000)
            self.assertRegex(self.helper("inspect")[0], r"^rc=0 existing=3 ", cut)
        self.assertEqual(seen, {False, True})

    def test_batch_limits(self):
        many = [(f"/Reprise/Many/f{i}.bin", b"x") for i in range(65)]
        result = self.helper("batch", many)
        self.assertEqual(result[:64], [f"rc=0 {p}" for p, _ in many[:64]])
        self.assertEqual(result[64:], ["rc=-324 /Reprise/Many/f64.bin"])
        twice = [("/Reprise/a.bin", b"1"), ("/Reprise/A.BIN", b"2")]
        self.assertEqual(
            self.helper("batch", twice),
            ["rc=0 /Reprise/a.bin", "rc=-324 /Reprise/A.BIN"],
        )
        self.assertEqual(
            self.helper("staged", [("/Reprise/a.bin", b"1")]),
            ["rc=0 /Reprise/a.bin", "rc=-318 close"],
        )
        with MountedVolume(self, self.disk) as mount:
            self.assertFalse((mount / "Reprise").exists())

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

    def test_disk_without_hfs_is_left_to_the_fat_checks(self):
        self.disk.write_bytes(bytes(len(self.base)))
        self.assertEqual(self.helper("install", [("/Reprise/a.bin", b"a")]), ["none"])
