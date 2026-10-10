/* SPDX-License-Identifier: GPL-3.0-only */
#include "layout.h"

static bool reject(struct disk_layout *layout, enum layout_error error, unsigned partition)
{
    layout->error = error;
    layout->failed_partition = partition;
    return false;
}

bool file_layout_ok(struct disk_layout *layout, uint64_t total, uint64_t *start,
                    uint64_t *end, unsigned *bytes)
{
    if (layout->error)
        return false;
    const uint8_t *m = layout->sectors[0];
    unsigned found = 0, scale = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *p = m + 446 + i * 16;
        if (p[4] != 0x0b && p[4] != 0x0c)
            continue;
        if (++found != 1)
            return reject(layout, LAYOUT_MULTIPLE_FAT32, i + 1);
        if (!(layout->mask & (1u << (i + 1))))
            return reject(layout, LAYOUT_BOOT_SECTOR_READ, i + 1);
        const uint8_t *v = layout->sectors[i + 1];
        *bytes = v[11] | ((unsigned)v[12] << 8);
        if (*bytes < 512 || *bytes > 4096 || (*bytes & (*bytes - 1)))
            return reject(layout, LAYOUT_SECTOR_SIZE, i + 1);
        /* MBR address units and FAT logical sectors can have different sizes. */
        scale = layout->partition_scale;
        if (scale != 1 && scale != 2 && scale != 4 && scale != 8)
            return reject(layout, LAYOUT_BOOT_SECTOR_LOCATION, i + 1);
        *start = (uint64_t)le32(p + 8) * scale;
        *end = *start + (uint64_t)le32(p + 12) * scale;
        uint32_t reserved = v[14] | ((uint32_t)v[15] << 8);
        uint32_t sectors = le32(v + 32), fat = le32(v + 36);
        uint64_t overhead = reserved + (uint64_t)v[16] * fat;
        unsigned cluster = v[13];
        if (!*start || *end <= *start || *end > total)
            return reject(layout, LAYOUT_PARTITION_BOUNDS, i + 1);
        if (layout->lba[i] != *start)
            return reject(layout, LAYOUT_BOOT_SECTOR_LOCATION, i + 1);
        if (!cluster || (cluster & (cluster - 1)))
            return reject(layout, LAYOUT_CLUSTER_SIZE, i + 1);
        if (!reserved)
            return reject(layout, LAYOUT_RESERVED_SECTORS, i + 1);
        if (!v[16] || v[16] > 2)
            return reject(layout, LAYOUT_FAT_COUNT, i + 1);
        if (!fat)
            return reject(layout, LAYOUT_FAT_SIZE, i + 1);
        if ((uint64_t)sectors * (*bytes / 512) > *end - *start)
            return reject(layout, LAYOUT_VOLUME_BOUNDS, i + 1);
        if (overhead >= sectors)
            return reject(layout, LAYOUT_DATA_REGION, i + 1);
        if (v[17] || v[18])
            return reject(layout, LAYOUT_ROOT_ENTRIES, i + 1);
        if (v[19] || v[20])
            return reject(layout, LAYOUT_FAT16_VOLUME, i + 1);
        if (v[22] || v[23])
            return reject(layout, LAYOUT_FAT16_SIZE, i + 1);
        if (v[42] || v[43])
            return reject(layout, LAYOUT_FAT_VERSION, i + 1);
        if (v[510] != 0x55 || v[511] != 0xaa)
            return reject(layout, LAYOUT_FAT_SIGNATURE, i + 1);
        uint64_t clusters = (sectors - overhead) / cluster;
        if (clusters < 65525 || clusters >= 0x0ffffff5)
            return reject(layout, LAYOUT_CLUSTER_COUNT, i + 1);
        if ((uint64_t)fat * *bytes / 4 < clusters + 2)
            return reject(layout, LAYOUT_FAT_CAPACITY, i + 1);
        if (le32(v + 44) < 2 || le32(v + 44) >= clusters + 2)
            return reject(layout, LAYOUT_ROOT_CLUSTER, i + 1);
    }
    if (found != 1)
        return reject(layout, LAYOUT_NO_FAT32, 0);
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *p = m + 446 + i * 16;
        if (!le32(p + 12) || p[4] == 0x0b || p[4] == 0x0c)
            continue;
        uint64_t first = (uint64_t)le32(p + 8) * scale;
        uint64_t last = first + (uint64_t)le32(p + 12) * scale;
        if (!first || last > total)
            return reject(layout, LAYOUT_OTHER_PARTITION_BOUNDS, i + 1);
        if (first < *end && last > *start)
            return reject(layout, LAYOUT_PARTITION_OVERLAP, i + 1);
    }
    return true;
}
