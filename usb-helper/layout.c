/* SPDX-License-Identifier: GPL-3.0-only */
#include "layout.h"

bool file_layout_ok(const struct disk_layout *layout, uint64_t total, uint64_t *start,
                    uint64_t *end, unsigned *bytes)
{
    const uint8_t *m = layout->sectors[0];
    unsigned found = 0, scale = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *p = m + 446 + i * 16;
        if (p[4] != 0x0b && p[4] != 0x0c)
            continue;
        if (++found != 1 || !(layout->mask & (1u << (i + 1))))
            return false;
        const uint8_t *v = layout->sectors[i + 1];
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
        if (!*start || *end > total || layout->lba[i] != *start || !cluster ||
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
