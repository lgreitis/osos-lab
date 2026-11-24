"""Host tests for the production HFS+ journal validation."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class JournalTest(unittest.TestCase):
    def test_journal_initialization_and_pending_transactions(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            (work / "config.h").write_text(
                "#define IF_MD(...)\n#define CACHEALIGN_ATTR\n"
            )
            (work / "system.h").write_text(
                "#include <stdint.h>\n#include <strings.h>\n"
                "#define MIN(a,b) ((a)<(b)?(a):(b))\n"
                "#define MAX(a,b) ((a)>(b)?(a):(b))\n"
            )
            (work / "ata.h").write_text(
                "#include <stdint.h>\n"
                "int ata_read_sectors(uint64_t,int,void *);\n"
                "int ata_write_sectors(uint64_t,int,const void *);\n"
                "int ata_flush(void);\n"
            )
            (work / "test.c").write_text(HARNESS)
            binary = work / "test"
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-I",
                    str(work),
                    "-I",
                    str(ROOT / "usb-helper"),
                    "-I",
                    str(ROOT / "loader/hfsplus"),
                    str(work / "test.c"),
                    str(ROOT / "loader/hfsplus/reader.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


HARNESS = r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "hfs-upload.c"

static uint8_t media[65536];
static unsigned reads;
int ata_read_sectors(uint64_t sector, int count, void *out)
{
    assert(sector * 512 + count * 512 <= sizeof(media));
    memcpy(out, media + sector * 512, count * 512);
    ++reads;
    return 0;
}
int ata_write_sectors(uint64_t sector, int count, const void *in)
{
    (void)sector; (void)count; (void)in;
    abort();
}
int ata_flush(void) { abort(); }

static void checksum(uint8_t *journal)
{
    uint32_t sum = 0;
    w32(journal + 36, 0);
    for (unsigned i = 0; i < 44; ++i)
        sum = (sum << 8) ^ (sum + journal[i]);
    w32(journal + 36, ~sum);
}

int main(void)
{
    disk.bytes = sizeof(media);
    vol.block_size = 512;
    vol.blocks = sizeof(media) / 512;
    vol.bytes = sizeof(media);
    w32(header + 4, 1u << 13);
    w32(header + 12, 2);
    uint8_t *info = media + 1024;
    w32(info, 5);
    w64(info + 36, 4096);
    w64(info + 44, 8192);
    assert(journal_clean());
    assert(reads == 1);
    w64(info + 36, sizeof(media) - 512);
    assert(!journal_clean());
    w64(info + 36, 4096);
    w32(info, 7);
    assert(!journal_clean());
    w32(info, 9);
    assert(!journal_clean());
    w32(info, 1);
    assert(!journal_clean());
    uint8_t *journal = media + 4096;
    memcpy(journal, "JNLx\x12\x34\x56\x78", 8);
    w64(journal + 8, 512);
    w64(journal + 16, 512);
    w64(journal + 24, 8192);
    w32(journal + 40, 512);
    checksum(journal);
    assert(journal_clean());
    w64(journal + 16, 1024);
    checksum(journal);
    assert(!journal_clean());
    w64(journal + 16, 512);
    checksum(journal);
    journal[36] ^= 1;
    assert(!journal_clean());
    return 0;
}
"""
