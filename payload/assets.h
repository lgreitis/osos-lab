/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

struct cfw_bitmap_asset {
    unsigned char *data;
    uint32_t size, crc;
};

extern const struct cfw_bitmap_asset cfw_bitmap_assets[];
extern const uint32_t cfw_bitmap_asset_count, cfw_theme_max_bytes;
extern const unsigned char cfw_theme_catalog_hash[32];

/* Prepare validates the next toggle; committing or cancelling frees its buffer. */
int cfw_assets_prepare(void);
int cfw_assets_enabled(void);
void cfw_assets_cancel(void);
void cfw_assets_set_enabled(int enabled);
