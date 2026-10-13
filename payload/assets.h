/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

struct osos_path;

struct cfw_theme_metadata {
    uint32_t accent;
    char name[48];
};

struct cfw_bitmap_asset {
    unsigned char *data;
    uint32_t size, start, pixels, colors, stride, width, height, depth, crc;
};

extern const struct cfw_bitmap_asset cfw_bitmap_assets[];
extern const uint32_t cfw_bitmap_asset_count, cfw_theme_max_bytes;
extern const unsigned char cfw_theme_catalog_hash[32];

int cfw_assets_prepare(void);
int cfw_assets_prepare_path(const struct osos_path *path);
int cfw_assets_metadata(const struct osos_path *path,
                        struct cfw_theme_metadata *metadata);
int cfw_assets_enabled(void);
uint32_t cfw_assets_accent(void);
void cfw_assets_cancel(void);
void cfw_assets_set_enabled(int enabled);
