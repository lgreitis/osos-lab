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
#include "ipod_nested.h"

extern void bss_init(void);

/* Patched with a new nonce by the host before each upload. */
static const volatile uint8_t nonce[16] = "REPRISE-NONCE-01";

static struct disk_layout layout;
struct storage_diagnostics upload_diagnostics;
struct storage_probe upload_probe;
struct layout_attempt upload_attempts[LAYOUT_ATTEMPTS];

static struct layout_attempt *new_attempt(unsigned slot, unsigned mult, uint32_t lba)
{
    struct layout_attempt *a = &upload_attempts[upload_probe.count++];
    memset(a, 0, sizeof(*a));
    a->slot = slot;
    a->multiplier = mult;
    a->lba = lba;
    a->rc = a->repeat_rc = -200;
    return a;
}

static int read_probe_sector(unsigned slot, unsigned mult, uint32_t lba, uint8_t *out)
{
    if (upload_probe.count >= LAYOUT_ATTEMPTS)
        return -202;
    struct layout_attempt *a = new_attempt(slot, mult, lba);
    a->rc = ata_read_sectors(IF_MD(0, ) lba, 1, out);
    if (!a->rc) {
        memcpy(a->sectors[0], out, 512);
        a->repeat_rc = ata_read_sectors(IF_MD(0, ) lba, 1, a->sectors[1]);
    }
    int rc = a->rc ? a->rc : a->repeat_rc;
    if (!rc) {
        a->matches = !memcmp(a->sectors[0], a->sectors[1], 512);
        if (!a->matches)
            rc = -201;
    }
    a->result = a->rc ? PROBE_READ_FAILED : a->repeat_rc ? PROBE_REPEAT_FAILED :
                !a->matches ? PROBE_MISMATCH : PROBE_HEADER_OK;
    return rc;
}

static int read_layout_sector(unsigned slot, unsigned mult, uint32_t lba)
{
    int rc = read_probe_sector(slot, mult, lba, layout.sectors[slot]);
    layout.rc[slot] = rc;
    if (!rc)
        layout.mask |= 1u << slot;
    return rc;
}

static bool apple_disk_header(const uint8_t *sector, uint32_t lba, uint64_t total)
{
    unsigned bytes = (unsigned)sector[2] << 8 | sector[3];
    uint32_t blocks = (uint32_t)sector[4] << 24 | (uint32_t)sector[5] << 16 |
                      (uint32_t)sector[6] << 8 | sector[7];
    /* Apple Driver Descriptor Record uses big-endian fields. */
    return sector[0] == 'E' && sector[1] == 'R' && bytes >= 512 && bytes <= 4096 &&
           !(bytes & (bytes - 1)) && blocks > 1 &&
           (uint64_t)lba + (uint64_t)blocks * (bytes / 512) <= total;
}

static void read_outer_layout(void)
{
    memset(&layout, 0, sizeof(layout));
    upload_probe.count = 0;
    for (unsigned i = 0; i < 5; ++i)
        layout.rc[i] = -200;
    if (RECORD->sector_size != 512) {
        layout.error = LAYOUT_DISK_SECTOR_SIZE;
        return;
    }
    if (read_layout_sector(0, 1, 0)) {
        layout.error = LAYOUT_MBR_READ;
        return;
    }
    const uint8_t *mbr = layout.sectors[0];
    uint64_t total = ((uint64_t)RECORD->sectors_high << 32) | RECORD->sectors_low;
    if (apple_disk_header(mbr, 0, total)) {
        layout.error = LAYOUT_APPLE_PARTITION_MAP;
        return;
    }
    if (mbr[510] != 0x55 || mbr[511] != 0xaa) {
        upload_attempts[0].result = PROBE_SIGNATURE;
        layout.error = LAYOUT_MBR_SIGNATURE;
        return;
    }
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
                (uint64_t)start * mult > UINT32_MAX) {
                uint64_t lba = (uint64_t)start * mult;
                struct layout_attempt *a = new_attempt(i + 1, mult, (uint32_t)lba);
                a->lba_high = lba >> 32;
                a->result = PROBE_BOUNDS;
                continue;
            }
            layout.lba[i] = start * mult;
            if (read_layout_sector(i + 1, mult, start * mult))
                continue;
            const uint8_t *v = layout.sectors[i + 1];
            unsigned bytes = v[11] | ((unsigned)v[12] << 8);
            if (v[510] == 0x55 && v[511] == 0xaa && bytes >= 512 &&
                bytes <= 4096 && !(bytes & (bytes - 1))) {
                scale = mult;
                layout.partition_scale = mult;
                fat_slot = i + 1;
                break;
            }
            upload_attempts[upload_probe.count - 1].result =
                v[510] != 0x55 || v[511] != 0xaa ? PROBE_SIGNATURE : PROBE_SECTOR_SIZE;
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
        if (!read_layout_sector(i + 1, scale, start * scale))
            upload_attempts[upload_probe.count - 1].result = PROBE_PARTITION_HEADER;
    }
}

static int read_nested_sector(void *context, uint32_t lba, unsigned mult,
                              bool boot, uint8_t *out)
{
    (void)context;
    int rc = read_probe_sector(boot ? 1 : 0, mult, lba, out);
    if (!rc)
        upload_attempts[upload_probe.count - 1].result =
            boot ? PROBE_NESTED_BOOT : PROBE_NESTED_MBR;
    return rc;
}

static void layout_put32(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        out[i] = value >> (i * 8);
}

static void read_layout(void)
{
    read_outer_layout();
    if (layout.error || layout.partition_scale)
        return;
    struct ipod_nested_partition p;
    uint8_t bpb[512] __attribute__((aligned(32)));
    uint64_t total = ((uint64_t)RECORD->sectors_high << 32) | RECORD->sectors_low;
    if (!ipod_nested_find(layout.sectors[0], total, read_nested_sector, NULL, &p, bpb)) {
        for (unsigned i = 0; i < upload_probe.count; ++i) {
            const struct layout_attempt *a = &upload_attempts[i];
            if (a->result == PROBE_NESTED_MBR &&
                apple_disk_header(a->sectors[0], a->lba, total)) {
                layout.error = LAYOUT_APPLE_PARTITION_MAP;
                layout.failed_partition = 0;
                break;
            }
        }
        return;
    }
    /* Normalize the effective table in RAM; retain the raw reads in diagnostics. */
    for (unsigned i = 0; i < 4; ++i) {
        uint8_t *entry = layout.sectors[0] + 446 + i * 16;
        layout_put32(entry + 8, le32(entry + 8) * p.mult);
        layout_put32(entry + 12, le32(entry + 12) * p.mult);
        layout.rc[i + 1] = -200;
        layout.lba[i] = 0;
        memset(layout.sectors[i + 1], 0, 512);
    }
    uint8_t *entry = layout.sectors[0] + 446 + p.slot * 16;
    layout_put32(entry + 8, p.start);
    layout_put32(entry + 12, p.size);
    memcpy(layout.sectors[p.slot + 1], bpb, 512);
    layout.rc[p.slot + 1] = 0;
    layout.lba[p.slot] = p.start;
    layout.mask = 1 | (1u << (p.slot + 1));
    layout.partition_scale = 1;
}

static void save_ata_diagnostics(const struct storage_info *info)
{
    extern void upload_ata_state(uint32_t *, uint32_t *);
    const unsigned short *id = ata_get_identify();
    upload_probe.magic = 0x53545031;
    upload_probe.version = 1;
    upload_ata_state(&upload_probe.flags, &upload_probe.pio_time);
    upload_probe.dma_mode = ata_get_dma_mode();
    upload_probe.logical_bytes = info->sector_size;
    upload_probe.driver_phys_mult = info->phys_sector_mult;
    upload_probe.identify106 = id[106];
    upload_probe.identify117 = id[117];
    upload_probe.identify118 = id[118];
    if (!(upload_probe.flags & 1)) {
        uint64_t logical = (id[106] & 0xd000) == 0x5000 ?
                           ((uint64_t)id[118] << 16 | id[117]) * 2 : 512;
        uint64_t physical = (id[106] & 0xe000) == 0x6000 ?
                            logical << (id[106] & 15) : logical;
        upload_probe.physical_bytes = physical <= UINT32_MAX ? physical : 0;
    }
    for (unsigned i = 0; i < 40; ++i)
        upload_probe.model[i] = id[27 + i / 2] >> (i % 2 ? 0 : 8);
    for (unsigned i = 0; i < 8; ++i)
        upload_probe.revision[i] = id[23 + i / 2] >> (i % 2 ? 0 : 8);
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
        save_ata_diagnostics(&info);
        read_layout();
        memset(UPLOAD_RESULT, 0, sizeof(*UPLOAD_RESULT));
        UPLOAD_RESULT->magic = 0x55504c32;
        UPLOAD_RESULT->version = 2;
        UPLOAD_RESULT->size = upload_config.size;
        for (unsigned i = 0; i < 16; ++i)
            UPLOAD_RESULT->nonce[i] = nonce[i];
        uint64_t start = 0, end = 0;
        unsigned bytes = 0;
        if (layout.error == LAYOUT_APPLE_PARTITION_MAP) {
            upload_prepare_hfs(info.num_sectors);
            if (!UPLOAD_RESULT->rc)
                layout.error = LAYOUT_OK;
        } else if (!file_layout_ok(&layout,
                                   ((uint64_t)RECORD->sectors_high << 32) |
                                       RECORD->sectors_low,
                                   &start, &end, &bytes))
            upload_fail(layout.error == LAYOUT_APPLE_PARTITION_MAP ? -323 : -301);
        else
            upload_prepare(start, end, bytes);
        save_diagnostics();
        RECORD->storage_rc = upload_usb();
        RECORD->phase = 4;
    }
    return_to_dfu();
}
