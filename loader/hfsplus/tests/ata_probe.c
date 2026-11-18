/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "reader.h"
#include <string.h>
#define REG(off) (*(volatile unsigned *)(0x38700000 + (off)))

static unsigned ata_read(unsigned reg)
{
    while (!(REG(0x78) & 2)) {
    }
    (void)REG(reg);
    while (!(REG(0x78) & 1)) {
    }
    return REG(0x7c);
}

static void ata_write(unsigned reg, unsigned value)
{
    while (!(REG(0x78) & 2)) {
    }
    REG(reg) = value;
}

static bool read_disk(void *ctx, uint64_t byte, void *target, size_t count)
{
    unsigned char sector[512], *out = target;
    (void)ctx;
    while (count) {
        if ((byte >> 9) >= (1u << 28))
            return false;
        unsigned lba = byte >> 9;
        ata_write(0x6c, 0xe0 | (lba >> 24));
        ata_write(0x5c, 1);
        ata_write(0x60, lba & 255);
        ata_write(0x64, (lba >> 8) & 255);
        ata_write(0x68, (lba >> 16) & 255);
        ata_write(0x70, 0x20);
        unsigned status;
        do {
            status = ata_read(0x74);
        } while (status & 128);
        if ((status & 9) != 8)
            return false;
        for (unsigned i = 0; i < 512; i += 2) {
            unsigned w = ata_read(0x54);
            sector[i] = w;
            sector[i + 1] = w >> 8;
        }
        unsigned off = byte & 511, n = 512 - off;
        if (n > count)
            n = count;
        memcpy(out, sector + off, n);
        out += n;
        byte += n;
        count -= n;
    }
    return true;
}

unsigned result[4];
static unsigned char scratch[32768];

__attribute__((noinline, noreturn)) void done(void)
{
    for (;;)
        asm volatile("");
}

void entry(void)
{
    struct hfs_disk disk = {read_disk, 0, DISK_BYTES};
    const char *names[] = {"osos-cfw.bin", "cfw-loader.bin"};
    unsigned addr[] = {0x0b000000, 0x0bc00000};
    for (int i = 0; i < 2; i++) {
        size_t len;
        result[2 * i] =
            hfs_read_root(&disk, names[i], (void *)addr[i], i ? 0x300000 : 0xc00000,
                          &len, scratch, sizeof scratch);
        result[2 * i + 1] = len;
    }
    done();
}
