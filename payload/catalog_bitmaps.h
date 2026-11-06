/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include "assets.h"

/* The payload's catalog bitmaps (catalog indices from cfw_bitmap_firmware_count up)
 * are same-size gray8 masks that share one BMap header and keep their pixels
 * run-packed. A run starts with a control byte whose low six bits hold its length
 * minus one: 0x00 for that many literal bytes that follow, 0x40 for zeros, 0x80 for
 * copies of the one byte that follows. Settings shows one preview at a time, so a
 * single resident slot holds the bitmap decoded last. */
struct cfw_catalog_source {
    const unsigned char *data;
    uint32_t size;
};

#define CFW_CATALOG_MAX_BITMAPS 16u

/* Generated: the built-in sources in rodata; the live sources, each the built-in
 * one or a theme's heap copy owned by the theme engine; the shared BMap header
 * (CFW_BMAP_HEADER_BYTES) and the slot (header plus cfw_catalog_pixel_bytes). */
extern const struct cfw_catalog_source cfw_catalog_builtin[];
extern struct cfw_catalog_source cfw_catalog_sources[];
extern const unsigned char cfw_catalog_header[];
extern unsigned char cfw_catalog_slot[];
extern const uint32_t cfw_catalog_bitmap_count, cfw_catalog_pixel_bytes,
    cfw_catalog_id_base;

/* BMap of the catalog bitmap at position (catalog index minus
 * cfw_bitmap_firmware_count), decoded into the slot unless it is already there.
 * NULL for an unknown position or a malformed source. */
const unsigned char *cfw_catalog_bitmap(uint32_t position);
/* Whether pixels is the pixel data in the slot; sets the position it holds. */
int cfw_catalog_slot_position(const unsigned char *pixels, uint32_t *position);
/* Decode the slot's bitmap again from its current source, so views showing the
 * slot follow the change; call after changing cfw_catalog_sources. The slot is
 * left empty if that source is malformed. */
void cfw_catalog_reload(void);
/* Packs cfw_catalog_pixel_bytes of pixels into out and returns the packed size;
 * with out NULL it only measures. */
uint32_t cfw_catalog_pack(const unsigned char *pixels, unsigned char *out);
