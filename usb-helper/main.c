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

static bool file_layout_ok(uint64_t *start, uint64_t *end, unsigned *bytes)
{
    const uint8_t *m = layout.sectors[0];
    uint64_t total = ((uint64_t)RECORD->sectors_high << 32) | RECORD->sectors_low;
    unsigned found = 0, scale = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *p = m + 446 + i * 16;
        if (p[4] != 0x0b && p[4] != 0x0c)
            continue;
        if (++found != 1 || !(layout.mask & (1u << (i + 1))))
            return false;
        const uint8_t *v = layout.sectors[i + 1];
        *bytes = v[11] | ((unsigned)v[12] << 8);
        if (*bytes < 512 || *bytes > 4096 || (*bytes & (*bytes - 1)))
            return false;
        scale = *bytes / 512;
        *start = (uint64_t)le32(p + 8) * scale;
        *end = *start + (uint64_t)le32(p + 12) * scale;
        uint32_t reserved = v[14] | ((uint32_t)v[15] << 8);
        uint32_t sectors = le32(v + 32), fat = le32(v + 36);
        uint64_t overhead = reserved + (uint64_t)v[16] * fat;
        unsigned cluster = v[13];
        if (!*start || *end > total || layout.lba[i] != *start || !cluster ||
            (cluster & (cluster - 1)) || !reserved || !v[16] || v[16] > 2 || !fat ||
            sectors > le32(p + 12) || overhead >= sectors || v[17] || v[18] || v[19] ||
            v[20] || v[22] || v[23] || v[42] || v[43] || v[510] != 0x55 ||
            v[511] != 0xaa)
            return false;
        uint64_t clusters = (sectors - overhead) / cluster;
        if (clusters < 65525 || clusters >= 0x0ffffff5 ||
            (uint64_t)fat * *bytes / 4 < clusters + 2 || le32(v + 44) < 2 ||
            le32(v + 44) >= clusters + 2)
            return false;
    }
    if (found != 1)
        return false;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *p = m + 446 + i * 16;
        if (!le32(p + 12) || p[4] == 0x0b || p[4] == 0x0c)
            continue;
        uint64_t first = (uint64_t)le32(p + 8) * scale;
        uint64_t last = first + (uint64_t)le32(p + 12) * scale;
        if (!first || last > total || (first < *end && last > *start))
            return false;
    }
    return true;
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
        for (unsigned i = 0; i < 16; ++i)
            UPLOAD_RESULT->nonce[i] = nonce[i];
        uint64_t start = 0, end = 0;
        unsigned bytes = 0;
        if (!file_layout_ok(&start, &end, &bytes))
            upload_fail(-301);
        else
            upload_prepare(start, end, bytes);
        RECORD->storage_rc = upload_usb();
        ata_sleepnow();
        RECORD->phase = 4;
    }
    return_to_dfu();
}
