/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

/* Bitmaps in catalog order: the firmware's resident ones, then the payload's
 * catalog bitmaps (RepriseOS previews that themes address like firmware bitmaps,
 * see catalog_bitmaps.h). A firmware bitmap starts at the previous one's end
 * rounded up to 4 bytes, so only sizes and checksums are stored. */
struct cfw_bitmap_asset {
    uint32_t size, crc;
};

/* Native BMap header size; pixel data (or the palette) follows it. */
#define CFW_BMAP_HEADER_BYTES 28u

extern const struct cfw_bitmap_asset cfw_bitmap_assets[];
/* Catalog indices of the option bar well center slices: the White style
 * (OptionBar_White_Well_Center_Image) then the Black style. */
extern const uint32_t cfw_bitmap_well_centers[2];
/* Catalog index of the Search text box center slice (System_Input_Field_Middle_Image). */
extern const uint32_t cfw_bitmap_field_center;
/* Catalog index of the status bar background (NowPlaying_StatusBar_Image). */
extern const uint32_t cfw_bitmap_status_bar;
/* Stock blue artwork that follows the accent while the active theme leaves it
 * stock: a paletted or ARGB catalog bitmap and the theme color role it takes. */
struct cfw_accent_art {
    uint16_t index;
    uint8_t role;
};
extern const struct cfw_accent_art cfw_bitmap_accent_art[];
extern const uint32_t cfw_bitmap_accent_art_count;
extern unsigned char *const cfw_bitmap_assets_base;
/* Catalog indices from cfw_bitmap_firmware_count up are the payload's. */
extern const uint32_t cfw_bitmap_asset_count, cfw_bitmap_firmware_count,
    cfw_theme_max_bytes;
extern const unsigned char cfw_theme_catalog_hash[32];
