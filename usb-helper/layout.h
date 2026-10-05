/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef REPRISE_LAYOUT_H
#define REPRISE_LAYOUT_H
#include <stdint.h>
#include <stdbool.h>

struct disk_layout {
    int32_t rc[5];
    uint32_t mask, lba[4];
    uint8_t sectors[5][512] __attribute__((aligned(32)));
};

static uint32_t le32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

bool file_layout_ok(const struct disk_layout *layout, uint64_t total, uint64_t *start,
                    uint64_t *end, unsigned *bytes);
#endif
