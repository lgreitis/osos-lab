/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

/* Resident firmware bitmaps in catalog order. Each bitmap starts at the previous
 * one's end rounded up to 4 bytes, so only sizes and checksums are stored. */
struct cfw_bitmap_asset {
    uint32_t size, crc;
};

extern const struct cfw_bitmap_asset cfw_bitmap_assets[];
extern unsigned char *const cfw_bitmap_assets_base;
extern const uint32_t cfw_bitmap_asset_count, cfw_theme_max_bytes;
extern const unsigned char cfw_theme_catalog_hash[32];
