/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include "theme_style.h"

struct osos_path;
#define CFW_THEME_STYLE_BYTES (sizeof(struct cfw_theme_style) + sizeof(struct cfw_theme_options))

struct cfw_theme_metadata {
    uint32_t magic;
    unsigned char catalog[32];
    uint32_t appearance_mask;
    char id[32], name[48];
};

/* An open RTH4 pack positioned after its prefix; file is NULL when closed. */
struct cfw_theme_reader {
    void *file;
    uint32_t size, count, crc;
};

/* Shared primitives for the RTH4/RTP1 codecs. The CRC runs on an inverted seed:
 * start from UINT32_MAX and invert the result. */
uint32_t cfw_theme_crc32(uint32_t crc, const void *data, uint32_t size);
uint32_t cfw_theme_u32(const unsigned char *p);
/* Volume 0 path from a NUL-terminated name shorter than the path buffer. */
void cfw_theme_path(struct osos_path *out, const char *text);

/* Prefix checks only: magic, catalog fingerprint, appearance, a file size that
 * matches the header, and for the baseline a record count and body size covering
 * the whole catalog. The style prefix of an appearance pack must also be opaque.
 * Records are checked when they are applied or restored. */
int cfw_theme_baseline_valid(const struct osos_path *path);
int cfw_theme_pack_valid(const struct osos_path *path, int dark);

/* Catalog index of the resident bitmap whose pixel data starts at pixels. */
int cfw_theme_asset_from_pixels(const unsigned char *pixels, uint32_t *index);
/* Resident BGRA palette of a paletted catalog bitmap, or NULL. */
unsigned char *cfw_theme_asset_palette(uint32_t index, uint32_t *colors);
/* Resident BGRA pixel rows of an ARGB catalog bitmap, or NULL. */
unsigned char *cfw_theme_asset_argb(uint32_t index, uint32_t *width, uint32_t *height,
                                    uint32_t *stride);
/* Opaque RGBA of the center pixel of a paletted or ARGB catalog bitmap. Returns 0
 * for masks, RGB565 bitmaps, invalid layouts or a fully transparent pixel. */
int cfw_theme_asset_center_pixel(uint32_t index, unsigned char out[4]);

int cfw_theme_metadata_read(const struct osos_path *path,
                            struct cfw_theme_metadata *out);
/* Opens an appearance pack and reads its CFW_THEME_STYLE_BYTES style prefix into
 * style, which must be 4-byte aligned. On failure nothing is open. */
int cfw_theme_pack_open(const struct osos_path *path, int dark,
                        struct cfw_theme_reader *reader, unsigned char *style);
void cfw_theme_reader_close(struct cfw_theme_reader *reader);
/* Streams the records of an open pack straight into resident artwork, checking
 * layout, palette indices, padding, tint coverage and the pack checksum. Every
 * asset whose bytes or catalog source may have changed is set in assets, also
 * on failure, when artwork is partially replaced. A failed record holds zeros, so
 * resident bitmaps never index past their palettes. Catalog bitmaps go through
 * cfw_catalog_slot; replacements that differ from the built-in art become heap
 * sources. The caller reloads the slot, serializes rendering, applies the style
 * and closes the reader. */
int cfw_theme_pack_apply(struct cfw_theme_reader *reader, const unsigned char *style,
                         uint32_t *assets);
/* Restores the original artwork of assets from the baseline. Catalog bitmaps get
 * their built-in sources back without a read, so the caller reloads the slot;
 * the baseline is seeked to each requested firmware bitmap, which must match its
 * original checksum. */
int cfw_theme_baseline_restore(const struct osos_path *path, const uint32_t *assets);
