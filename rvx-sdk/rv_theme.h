/* SPDX-License-Identifier: GPL-3.0-only */

/* A theme's say over every extension's bitmap assets, the core's own included,
 * through api->asset_set_provider. */

#ifndef RV_THEME_H
#define RV_THEME_H
#include "rv_api.h"

/* `owner` and `asset` are the extension's id and the asset's name as the core
 * hashes strings (32-bit FNV-1a); the core's own assets are owned by the hash
 * of "revola". A bitmap is a whole BMap, its 28-byte header and its rows, as
 * the core decodes it from the package. The provider hands it over as stored,
 * raw or as one lz stream in the package format, and the core decodes it like
 * the extension's own. */
enum { RV_ASSET_RAW, RV_ASSET_LZ };

struct rv_asset_stream {
    uint8_t codec;   /* RV_ASSET_RAW or RV_ASSET_LZ */
    uint32_t stored; /* bytes as stored */
    uint32_t bytes;  /* bytes once decoded: the whole BMap */
};

struct rv_asset_provider {
    /* 1 and *stream filled when the provider holds a bitmap for the asset. */
    int (*replacement)(uint32_t owner, uint32_t asset, struct rv_asset_stream *stream);
    /* Copies the `stored` bytes of that bitmap into `out`; 1 when it did, 0
     * leaves the core to decode the extension's own. */
    int (*replacement_read)(uint32_t owner, uint32_t asset, void *out, uint32_t stored);
    /* A decoded bitmap, the provider's replacement or the extension's own,
     * which it may alter in place; 1 when it did. */
    int (*adjust)(uint32_t owner, uint32_t asset, void *bmap, uint32_t bytes);
};

#endif
