/* SPDX-License-Identifier: GPL-3.0-only */
/* Initialization adapted from Rockbox bootloader/ipod-s5l87xx.c:
 * https://github.com/Rockbox/rockbox (GPL-2.0-or-later).
 * Copyright (C) 2005 by Dave Chapman.
 */
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "../kernel-internal.h"
#include "storage.h"
#include "ata.h"
#include "power.h"
#include "powermgmt.h"
#include "i2c-s5l8702.h"
#include "clocking-s5l8702.h"
#include "pmu-target.h"
#include "string.h"
#include "record.h"
#include "return_blob.h"
#include "upload.h"
#include "layout.h"

extern void bss_init(void);

/* Patched with a new nonce by the host before each upload. */
static const volatile uint8_t nonce[16] = "REPRISE-NONCE-01";

static struct disk_layout layout;
struct storage_diagnostics upload_diagnostics;
static uint8_t readback[512] __attribute__((aligned(32)));

static int read_layout_sector(unsigned slot, uint32_t lba)
{
    int rc = ata_read_sectors(IF_MD(0, ) lba, 1, layout.sectors[slot]);
    if (!rc)
        rc = ata_read_sectors(IF_MD(0, ) lba, 1, readback);
    if (!rc && memcmp(readback, layout.sectors[slot], 512))
        rc = -201;
    layout.rc[slot] = rc;
    if (!rc)
        layout.mask |= 1u << slot;
    return rc;
}

static void read_layout(void)
{
    memset(&layout, 0, sizeof(layout));
    for (unsigned i = 0; i < 5; ++i)
        layout.rc[i] = -200;
    if (RECORD->sector_size != 512) {
        layout.error = LAYOUT_DISK_SECTOR_SIZE;
        return;
    }
    if (read_layout_sector(0, 0)) {
        layout.error = LAYOUT_MBR_READ;
        return;
    }
    const uint8_t *mbr = layout.sectors[0];
    if (mbr[510] != 0x55 || mbr[511] != 0xaa) {
        layout.error = LAYOUT_MBR_SIGNATURE;
        return;
    }
    uint64_t total = ((uint64_t)RECORD->sectors_high << 32) | RECORD->sectors_low;
    unsigned scale = 0, fat_slot = 0;
    /* Rockbox tries 512–4096-byte partition units on this target. */
    for (unsigned i = 0; i < 4 && !scale; ++i) {
        const uint8_t *entry = mbr + 446 + i * 16;
        if (entry[4] != 0x0b && entry[4] != 0x0c)
            continue;
        uint32_t start = le32(entry + 8), count = le32(entry + 12);
        if (!start || !count || (uint64_t)start + count > total) {
            layout.error = LAYOUT_PARTITION_BOUNDS;
            layout.failed_partition = i + 1;
            return;
        }
        for (unsigned mult = 1; mult <= 8; mult <<= 1) {
            if (((uint64_t)start + count) * mult > total ||
                (uint64_t)start * mult > UINT32_MAX)
                continue;
            layout.lba[i] = start * mult;
            if (read_layout_sector(i + 1, start * mult))
                continue;
            const uint8_t *v = layout.sectors[i + 1];
            unsigned bytes = v[11] | ((unsigned)v[12] << 8);
            if (v[510] == 0x55 && v[511] == 0xaa && bytes == 512 * mult) {
                scale = mult;
                fat_slot = i + 1;
                break;
            }
        }
    }
    if (!scale)
        return;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *entry = mbr + 446 + i * 16;
        uint32_t start = le32(entry + 8), count = le32(entry + 12);
        if (!count || !start || ((uint64_t)start + count) * scale > total ||
            (uint64_t)start * scale > UINT32_MAX)
            continue;
        layout.lba[i] = start * scale;
        if (i + 1 == fat_slot)
            continue;
        read_layout_sector(i + 1, start * scale);
    }
}

static void save_diagnostics(void)
{
    upload_diagnostics.magic = 0x53544731;
    upload_diagnostics.version = 1;
    upload_diagnostics.sector_bytes = RECORD->sector_size;
    upload_diagnostics.sectors_low = RECORD->sectors_low;
    upload_diagnostics.sectors_high = RECORD->sectors_high;
    upload_diagnostics.battery_mv = RECORD->battery_mv;
    upload_diagnostics.layout_error = layout.error;
    upload_diagnostics.failed_partition = layout.failed_partition;
    upload_diagnostics.read_mask = layout.mask;
    memcpy(upload_diagnostics.read_rc, layout.rc, sizeof(layout.rc));
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *p = layout.sectors[0] + 446 + i * 16;
        const uint8_t *v = layout.sectors[i + 1];
        upload_diagnostics.partitions[i].type = p[4];
        upload_diagnostics.partitions[i].start = le32(p + 8);
        upload_diagnostics.partitions[i].count = le32(p + 12);
        upload_diagnostics.partitions[i].read_lba = layout.lba[i];
        upload_diagnostics.partitions[i].sector_bytes = v[11] | ((unsigned)v[12] << 8);
        upload_diagnostics.partitions[i].cluster_sectors = v[13];
        upload_diagnostics.partitions[i].reserved_sectors = v[14] | ((unsigned)v[15] << 8);
        upload_diagnostics.partitions[i].fats = v[16];
        upload_diagnostics.partitions[i].volume_sectors = le32(v + 32);
        upload_diagnostics.partitions[i].fat_sectors = le32(v + 36);
        upload_diagnostics.partitions[i].root_cluster = le32(v + 44);
        upload_diagnostics.partitions[i].flags = v[40] | ((unsigned)v[41] << 8);
        upload_diagnostics.partitions[i].root_entries = v[17] | ((unsigned)v[18] << 8);
        upload_diagnostics.partitions[i].volume_sectors16 = v[19] | ((unsigned)v[20] << 8);
        upload_diagnostics.partitions[i].fat_sectors16 = v[22] | ((unsigned)v[23] << 8);
        upload_diagnostics.partitions[i].version = v[42] | ((unsigned)v[43] << 8);
    }
}

static void return_to_dfu(void) __attribute__((noreturn));

static void return_to_dfu(void)
{
    disable_irq();
    disable_fiq();
    memcpy((void *)RETURN_ADDR, return_blob, sizeof(return_blob));
    commit_discard_idcache();
    ((void (*)(void))RETURN_ADDR)();
    while (1)
        ;
}

void main(void)
{
    usec_timer_init();
    i2c_preinit(0);
    memset((void *)RECORD, 0, sizeof(*RECORD));
    RECORD->magic = RECORD_MAGIC;
    RECORD->version = 1;
    RECORD->phase = 1;
    RECORD->storage_rc = -100;
    for (unsigned i = 0; i < sizeof(nonce); ++i)
        RECORD->nonce[i] = nonce[i];
    if (pmu_is_hibernated())
        return_to_dfu();
    system_preinit();
    memory_init();
    bss_init();
    system_init();
    kernel_init();
    i2c_init();
    power_init();
    enable_irq();
    RECORD->battery_mv = _battery_voltage();
    if (RECORD->battery_mv < 3600) {
        RECORD->storage_rc = -101;
        return_to_dfu();
    }

    RECORD->phase = 2;
    /* Use the driver directly to avoid starting the background storage thread. */
    int rc = ata_init();
    RECORD->storage_rc = rc;
    if (rc == 0) {
        struct storage_info info;
        storage_get_info(0, &info);
        RECORD->sector_size = info.sector_size;
        RECORD->sectors_low = (uint32_t)info.num_sectors;
        RECORD->sectors_high = (uint32_t)((uint64_t)info.num_sectors >> 32);
        RECORD->phase = 3;
        read_layout();
        memset(UPLOAD_RESULT, 0, sizeof(*UPLOAD_RESULT));
        UPLOAD_RESULT->magic = 0x55504c32;
        UPLOAD_RESULT->version = 2;
        UPLOAD_RESULT->size = upload_config.size;
        for (unsigned i = 0; i < 16; ++i)
            UPLOAD_RESULT->nonce[i] = nonce[i];
        uint64_t start = 0, end = 0;
        unsigned bytes = 0;
        if (!file_layout_ok(
                &layout, ((uint64_t)RECORD->sectors_high << 32) | RECORD->sectors_low,
                &start, &end, &bytes))
            upload_fail(-301);
        else
            upload_prepare(start, end, bytes);
        save_diagnostics();
        RECORD->storage_rc = upload_usb();
        RECORD->phase = 4;
    }
    return_to_dfu();
}
