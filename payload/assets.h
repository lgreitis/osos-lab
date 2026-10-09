/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

struct cfw_bitmap_asset {
    uint32_t id;
    const unsigned char *data;
    const unsigned char *original;
};

extern struct cfw_bitmap_asset cfw_bitmap_assets[];
extern const uint32_t cfw_bitmap_asset_count;
