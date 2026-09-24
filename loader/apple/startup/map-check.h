/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#define MAP_CAPACITY 0x4000u
#define STAGE_START 0x0b000000u
#define DRAM_END 0x0c000000u

static uint32_t word(const void *p, unsigned offset)
{
    const unsigned char *b = p;
    return (uint32_t)b[offset] | (uint32_t)b[offset + 1] << 8 |
           (uint32_t)b[offset + 2] << 16 | (uint32_t)b[offset + 3] << 24;
}

static int valid_map(const unsigned char *map, uint32_t bytes, uint32_t stride,
                     uint32_t version)
{
    if (!bytes || bytes > MAP_CAPACITY || stride != 48 || bytes % 48 || version != 1)
        return 0;
    uint32_t pages = 0, conventional = 0;
    for (uint32_t i = 0; i < bytes; i += 48) {
        const unsigned char *d = map + i;
        uint32_t type = word(d, 0), start = word(d, 8), count = word(d, 24);
        if (type > 13 || word(d, 12) || word(d, 28) || !count || count > 0x100000 ||
            (start & 0xfff))
            return 0;
        uint64_t end = (uint64_t)start + ((uint64_t)count << 12);
        if (end > 0x100000000ULL || (start < DRAM_END && end > STAGE_START))
            return 0; /* Reject descriptors overlapping reserved DRAM. */
        if (start < STAGE_START && end > 0x08000000) {
            if (start < 0x08000000 || end > STAGE_START)
                return 0;
            pages += count;
            if (type == 7)
                conventional += count;
        }
        for (uint32_t j = 0; j < i; j += 48) {
            uint32_t other = word(map + j, 8);
            uint64_t other_end = (uint64_t)other + ((uint64_t)word(map + j, 24) << 12);
            if (start < other_end && other < end)
                return 0;
        }
    }
    return pages == 0x3000 && conventional != 0;
}
