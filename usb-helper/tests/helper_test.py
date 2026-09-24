#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline SHA and staged-file tests against the helper's C implementation."""

import ctypes
import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]

MOCK = r"""
#ifndef TEST_MOCK_H
#define TEST_MOCK_H
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define IF_MV(...)
typedef uint64_t sector_t;
struct partinfo { uint64_t start, size; unsigned type; };
void filesystem_init(void);
int disk_mount_all(void);
int disk_unmount_all(void);
bool disk_partinfo(int, struct partinfo *);
unsigned fat_get_bytes_per_sector(void);
bool fat_size(sector_t *, sector_t *);
unsigned fat_get_cluster_size(void);
void ata_sleepnow(void);
bool file_exists(const char *);
int test_open(const char *, int, ...);
int test_close(int);
ssize_t test_read(int, void *, size_t);
ssize_t test_write(int, const void *, size_t);
int test_fsync(int);
int test_rename(const char *, const char *);
off_t filesize(int);
int upload_write_allowed(uint64_t, int);
#ifndef TEST_IMPLEMENTATION
#define open test_open
#define close test_close
#define read test_read
#define write test_write
#define fsync test_fsync
#define rename test_rename
#endif
#endif
"""
HARNESS = r"""
#define TEST_IMPLEMENTATION
#include "mock.h"
#include "upload.h"
#include "sha256.h"
uint8_t test_buffer[65536] __attribute__((aligned(32)));
struct upload_result test_result;
struct storage_result test_storage;
static int fault, mounts, sleeps, opens;
static bool corrupt;
void filesystem_init(void) {}
int disk_mount_all(void) { mounts++; return 1; }
int disk_unmount_all(void) { return fault==6 ? 0 : 1; }
bool disk_partinfo(int n, struct partinfo *p) { (void)n; *p=(struct partinfo){394224,1000995848,0x0c}; return true; }
unsigned fat_get_bytes_per_sector(void) {return 4096;}
bool fat_size(sector_t *s, sector_t *f) {(void)s; *f=fault==8 ? 0 : 1000000; return true;}
unsigned fat_get_cluster_size(void) {return 16384;}
void ata_sleepnow(void) {sleeps++;}
bool file_exists(const char *p) {return access(p+1,F_OK)==0;}
int test_open(const char *p,int flags,...) {opens++; return open(p+1,flags,0600);}
int test_close(int fd) {int r=close(fd);return fault==5 ? -1 : r;}
ssize_t test_read(int fd,void *p,size_t n) {ssize_t r=read(fd,p,n);if(fault==1 && !corrupt && r>0){((uint8_t*)p)[0]^=1;corrupt=true;}return r;}
ssize_t test_write(int fd,const void *p,size_t n) {assert(upload_write_allowed(394224,1));if(fault==2)return -1;return write(fd,p,n);}
int test_fsync(int fd) {if(fault==3)return -1;return fsync(fd);}
int test_rename(const char *a,const char *b) {assert(upload_write_allowed(394224,1));if(fault==4)return -1;return rename(a+1,b+1);}
off_t filesize(int fd) {struct stat s;assert(!fstat(fd,&s));return s.st_size;}
static void original(void) {int fd=open("os.bin",O_RDONLY);assert(fd>=0);char b[4]={0};assert(read(fd,b,4)==3);assert(!memcmp(b,"old",3));assert(!close(fd));}
static void pattern(uint32_t off,unsigned n) {for(unsigned i=0;i<n;i++)test_buffer[i]=(off+i)*37+(off+i)/11;}
int main(int argc,char **argv)
{
    assert(argc==5);
    unsigned size=strtoul(argv[1],0,10); unsigned overwrite=atoi(argv[2]); bool exists=atoi(argv[3]);fault=atoi(argv[4]);
    if(exists){int f=open("os.bin",O_WRONLY|O_CREAT,0600);assert(write(f,"old",3)==3);close(f);}
    upload_config.size=size;upload_config.overwrite=overwrite;strcpy(upload_config.path,"/os.bin");
    struct sha256 hash;sha256_init(&hash);
    for(unsigned off=0;off<size;){unsigned n=MIN(size-off,65536);pattern(off,n);sha256_update(&hash,test_buffer,n);off+=n;}
    sha256_final(&hash,upload_config.sha256);
    if(fault==7)upload_config.sha256[0]^=1;
    memset(test_result.nonce,7,16);
    upload_prepare(394224,1001390072,4096);
    if(fault==8){assert(test_result.rc==-322);assert(opens==0);upload_close();return 0;}
    if(overwrite==2){assert(test_result.state==4);assert(!test_result.rc);assert(opens==0);assert(test_storage.start==394224);assert(test_storage.end==1001390072);assert(test_storage.free_bytes==1024000000);assert(!upload_write_allowed(394224,1));upload_close();return 0;}
    if(exists&&!overwrite){assert(test_result.rc==-304);assert(opens==0);original();upload_close();return 0;}
    assert(test_result.state==1);test_result.state=2;upload_start();
    for(unsigned off=0;off<size&&test_result.state==2;){
        unsigned n=MIN(size-off,65536);pattern(off,n);test_result.received+=n;upload_write(n);off+=n;
        if(exists)original();
    }
    while(test_result.state==3){if(exists)original();upload_verify_step();}
    upload_close();
    assert(test_result.state==4);
    if(fault){assert(test_result.rc);if(exists)original();return 0;}
    assert(!test_result.rc);assert(!test_result.rejected);
    assert(test_result.received==size && test_result.written==size && test_result.verified==size);
    assert(!memcmp(test_result.source_sha256,upload_config.sha256,32));
    assert(!memcmp(test_result.disk_sha256,upload_config.sha256,32));
    assert(mounts==2 && sleeps>=2);
    int f=open("os.bin",O_RDONLY);assert(f>=0);assert(filesize(f)==size);
    uint8_t readback[65536];
    for(unsigned off=0;off<size;){unsigned n=MIN(size-off,65536);assert(read(f,readback,n)==n);pattern(off,n);assert(!memcmp(test_buffer,readback,n));off+=n;}
    close(f);
    assert(!upload_write_allowed(394224,1));
    return 0;
}
"""


class SHA(ctypes.Structure):
    _fields_ = [
        ("h", ctypes.c_uint32 * 8),
        ("bytes", ctypes.c_uint64),
        ("tail", ctypes.c_uint8 * 64),
    ]


class HelperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.tmp.name)
        p = cls.root
        subprocess.run(
            [
                "cc",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-shared",
                "-fPIC",
                str(HERE / "sha256.c"),
                "-o",
                str(p / "sha.so"),
            ],
            check=True,
        )
        cls.sha = ctypes.CDLL(str(p / "sha.so"))
        (p / "mock.h").write_text(MOCK)
        for name in [
            "config.h",
            "system.h",
            "file.h",
            "file_internal.h",
            "disk.h",
            "fat.h",
            "ata.h",
        ]:
            (p / name).write_text('#include "mock.h"\n')
        header = (
            (HERE / "upload.h")
            .read_text()
            .replace(
                "#define UPLOAD_DATA ((uint8_t *)0x09800000u)",
                "extern uint8_t test_buffer[65536] __attribute__((aligned(32)));\n#define UPLOAD_DATA test_buffer",
            )
            .replace(
                "#define UPLOAD_RESULT ((struct upload_result *)0x2201fa80u)",
                "extern struct upload_result test_result;\n#define UPLOAD_RESULT (&test_result)",
            )
            .replace(
                "extern const volatile struct upload_config",
                "extern struct upload_config",
            )
        )
        header = header.replace(
            "#define STORAGE_RESULT ((struct storage_result *)0x2201fbc0u)",
            "extern struct storage_result test_storage;\n#define STORAGE_RESULT (&test_storage)",
        )
        (p / "upload.h").write_text(header)
        (p / "file.c").write_text(
            (HERE / "file.c")
            .read_text()
            .replace("const volatile struct upload_config", "struct upload_config")
        )
        (p / "harness.c").write_text(HARNESS)
        subprocess.run(
            [
                "cc",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I" + str(p),
                "-I" + str(HERE),
                str(p / "harness.c"),
                str(p / "file.c"),
                str(HERE / "sha256.c"),
                "-o",
                str(p / "file-test"),
            ],
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_incremental_sha(self):
        for size in [
            *range(130),
            511,
            512,
            513,
            65535,
            65536,
            65537,
            13 * 1024 * 1024 + 17,
        ]:
            data = (bytes(range(256)) * (size // 256 + 1))[:size]
            for chunk in [1, 63, 512, 65536] if size < 100000 else [65536]:
                s = SHA()
                self.sha.sha256_init(ctypes.byref(s))
                for off in range(0, size, chunk):
                    part = data[off : off + chunk]
                    self.sha.sha256_update(
                        ctypes.byref(s),
                        ctypes.c_char_p(part),
                        ctypes.c_size_t(len(part)),
                    )
                out = ctypes.create_string_buffer(32)
                self.sha.sha256_final(ctypes.byref(s), out)
                self.assertEqual(out.raw, hashlib.sha256(data).digest(), (size, chunk))

    def run_file(self, size, overwrite, exists, fault):
        with tempfile.TemporaryDirectory(dir=self.root) as directory:
            subprocess.run(
                [
                    str(self.root / "file-test"),
                    str(size),
                    str(int(overwrite)),
                    str(int(exists)),
                    str(fault),
                ],
                cwd=directory,
                check=True,
            )

    def test_storage_inspection_is_read_only(self):
        self.run_file(12000000, 2, True, 0)

    def test_sizes_and_replacement(self):
        for size in [0, 1, 511, 512, 513, 65535, 65536, 65537, 13 * 1024 * 1024 + 17]:
            for overwrite, exists in [(False, False), (False, True), (True, True)]:
                with self.subTest(size=size, overwrite=overwrite, exists=exists):
                    self.run_file(size, overwrite, exists, 0)

    def test_failures_preserve_existing_file(self):
        for fault in range(1, 9):
            with self.subTest(fault=fault):
                self.run_file(65537, True, True, fault)


if __name__ == "__main__":
    unittest.main()
