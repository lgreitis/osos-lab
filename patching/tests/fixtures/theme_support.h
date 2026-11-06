/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#define CFW_OSOS_H
#include "../../../payload/assets.h"
#include "../../../payload/theme_package.h"
#include "../../../payload/theme_id.c"

/* Five resident bitmaps, packed with the firmware's 4-byte alignment: two 42-byte
 * paletted (format 0x64, two colors), one 42-byte 14x1 gray mask (format 8),
 * one 36-byte 2x1 ARGB (format 0x1888: an opaque dark pixel, then a transparent one)
 * and, standing in for the payload's catalog bitmap block, one 32-byte 8x1 4-bit
 * mask (format 4). */
#define ASSETS 5
#define FIRMWARE_ASSETS 4
#define MASK_ASSET 2
#define ARGB_ASSET 3
#define PAYLOAD_ASSET 4
static unsigned char pixels[ASSETS][44] = {
    {100, 0, 0, 0,  2, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0,   0, 1, 0, 0,   0, 2,
     0,   0, 0, 14, 0, 0, 0, 2, 0, 0, 0, 3, 2, 1, 255, 6, 5, 4, 255, 0, 1},
    {100, 0, 0, 0,  2, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0,   0, 1, 0, 0,   0, 2,
     0,   0, 0, 14, 0, 0, 0, 2, 0, 0, 0, 3, 2, 1, 255, 6, 5, 4, 255, 0, 1},
    {8, 0, 0, 0,  14, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,  0,  0,  14,
     0, 0, 0, 14, 0,  0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13},
    {136, 24, 0, 0, 8, 0, 32, 0, 0, 0, 0,  0,  0,  0,   0, 0, 1, 0,
     0,   0,  2, 0, 0, 0, 8,  0, 0, 0, 40, 30, 20, 255, 0, 0, 0, 0},
    {4, 0, 0, 0, 4, 0, 4, 0, 0, 0, 0, 0, 0,    0,    0,    0,
     1, 0, 0, 0, 8, 0, 0, 0, 4, 0, 0, 0, 0x12, 0x34, 0x56, 0x78}};
static unsigned char original[ASSETS][44];
const struct cfw_bitmap_asset cfw_bitmap_assets[] = {{42, 0x25789516},
                                                     {42, 0x25789516},
                                                     {42, 0x3c33b0ab},
                                                     {36, 0x5130d0ab},
                                                     {32, 0x03a0153c}};
/* The ARGB asset stands in for both option bar well center slices. */
const uint32_t cfw_bitmap_well_centers[2] = {ARGB_ASSET, ARGB_ASSET};
/* The first paletted asset stands in for the Search field center slice. */
const uint32_t cfw_bitmap_field_center = 0;
unsigned char *const cfw_bitmap_assets_base = pixels[0];
unsigned char *const cfw_catalog_bitmaps_base = pixels[PAYLOAD_ASSET];
const uint32_t cfw_bitmap_asset_count = ASSETS,
               cfw_bitmap_firmware_count = FIRMWARE_ASSETS,
               cfw_theme_max_bytes = 24 + 24 + 28 + 20 + 16;
const unsigned char cfw_theme_catalog_hash[32] = {1, 2, 3};

/* BitmapView stub for theme_tint.c: the loaded pixel data and the highlighted flag. */
struct osos_view_stub {
    const unsigned char *pixels;
    int highlighted;
};

static inline const unsigned char *osos_bitmap_view_pixels(const void *view)
{
    return ((const struct osos_view_stub *)view)->pixels;
}

/* Fixture blits pass the view stub as its own bitmap context. */
static inline const unsigned char *osos_bitmap_context_pixels(const void *context)
{
    return osos_bitmap_view_pixels(context);
}

static inline int osos_view_highlighted(const void *view)
{
    return ((const struct osos_view_stub *)view)->highlighted;
}

struct osos_path {
    uint16_t volume;
    char text[258];
};

static struct file {
    unsigned char data[2048];
    unsigned size, cursor;
} files[4];

static unsigned missing_files;

static int fail_alloc, allocations, reads_fail, short_reads, opens, closes;

static unsigned alloc_attempts, fail_alloc_at, live_bytes, peak_bytes;
static void *allocated[4];
static unsigned allocated_sizes[4];

static void *osos_import_alloc(uint32_t size)
{
    alloc_attempts++;
    if (fail_alloc || (fail_alloc_at && alloc_attempts == fail_alloc_at))
        return NULL;
    void *p = malloc(size);
    assert(p);
    unsigned slot = 0;
    while (slot < 4 && allocated[slot])
        slot++;
    assert(slot < 4);
    allocated[slot] = p;
    allocated_sizes[slot] = size;
    allocations++;
    live_bytes += size;
    if (live_bytes > peak_bytes)
        peak_bytes = live_bytes;
    return p;
}

static void osos_import_free(void *data)
{
    unsigned slot = 0;
    while (slot < 4 && allocated[slot] != data)
        slot++;
    assert(slot < 4);
    live_bytes -= allocated_sizes[slot];
    allocated[slot] = NULL;
    allocations--;
    free(data);
}

static int osos_file_open(const struct osos_path *p, uint32_t mode, void **out)
{
    assert(mode == 1);
    unsigned n = !strcmp(p->text, "themes/classic/dark.bin")      ? 3
                 : !strcmp(p->text, "themes/classic/default.bin") ? 1
                 : !strncmp(p->text, "themes/", 7)                ? 0
                                                                  : p->text[0] - '0';
    assert(n < 4);
    if (missing_files & (1u << n))
        return -43;
    files[n].cursor = 0;
    *out = &files[n];
    opens++;
    return 0;
}

static int osos_file_read(void *handle, void *out, uint32_t size, uint32_t *read)
{
    struct file *f = handle;
    if (reads_fail) {
        *read = 0;
        return -1;
    }
    if (f->cursor == f->size) {
        *read = 0;
        return -39;
    }
    if (size > f->size - f->cursor)
        size = f->size - f->cursor;
    if (short_reads && size)
        size--;
    memcpy(out, f->data + f->cursor, size);
    f->cursor += size;
    *read = size;
    return 0;
}

static void osos_file_close(void *handle)
{
    (void)handle;
    closes++;
}

#include "../../../payload/theme_package.c"
static const struct osos_path incoming = {0, "0"}, baseline = {0, "1"},
                              metadata = {0, "2"};
static uint32_t active_bits[CFW_THEME_BITMAP_WORDS];

/* Palette assets replace 10 bytes from offset 32; the mask replaces 14 from 28,
 * the ARGB asset 8 from 28 and the payload mask 4 from 28. */
static unsigned record_size(unsigned asset)
{
    return asset == MASK_ASSET      ? 12 + 16
           : asset == ARGB_ASSET    ? 12 + 8
           : asset == PAYLOAD_ASSET ? 12 + 4
                                    : 12 + 12;
}

static void write_record(unsigned char *out, unsigned asset, int recolor)
{
    struct record *r = (void *)out;
    r->asset = asset;
    r->offset =
        asset == MASK_ASSET || asset == ARGB_ASSET || asset == PAYLOAD_ASSET ? 28 : 32;
    r->size = cfw_bitmap_assets[asset].size - r->offset;
    memset(r->data, 0, record_size(asset) - 12);
    memcpy(r->data, original[asset] + r->offset, r->size);
    if (recolor)
        r->data[0] = 99;
}

static void opaque_style(unsigned char *body)
{
    for (unsigned i = 0; i < CFW_THEME_COLOR_COUNT; i++)
        body[i * 4 + 3] = 255;
    unsigned end =
        sizeof(struct cfw_theme_style) + offsetof(struct cfw_theme_options, tint);
    for (unsigned i = sizeof(struct cfw_theme_style) + 7; i < end; i += 4)
        body[i] = 255;
}

/* RTH4 pack with one record per asset from 0; recolor marks replaced artwork. */
static void make_pack(struct file *f, uint32_t appearance, unsigned count, int recolor)
{
    memset(f, 0, sizeof(*f));
    struct pack_header *h = (void *)f->data;
    h->magic = 0x34485452;
    memcpy(h->catalog, cfw_theme_catalog_hash, 32);
    h->appearance = appearance;
    h->size = CFW_THEME_STYLE_BYTES;
    h->count = count;
    unsigned char *body = f->data + sizeof(*h);
    opaque_style(body);
    for (unsigned i = 0; i < count; i++) {
        write_record(body + h->size, i, recolor);
        h->size += record_size(i);
    }
    h->crc = ~cfw_theme_crc32(UINT32_MAX, body, h->size);
    f->size = sizeof(*h) + h->size;
}

static void make_target(unsigned asset)
{
    make_pack(&files[0], 1, 0, 0);
    struct pack_header *h = (void *)files[0].data;
    unsigned char *body = files[0].data + sizeof(*h);
    h->size = CFW_THEME_STYLE_BYTES + record_size(asset);
    h->count = 1;
    write_record(body + CFW_THEME_STYLE_BYTES, asset, 1);
    h->crc = ~cfw_theme_crc32(UINT32_MAX, body, h->size);
    files[0].size = sizeof(*h) + h->size;
}

static void make_baseline(void)
{
    make_pack(&files[1], 0, ASSETS, 0);
}

static void fix_crc(void)
{
    struct pack_header *h = (void *)files[0].data;
    h->crc = ~cfw_theme_crc32(UINT32_MAX, files[0].data + sizeof(*h), h->size);
}

static inline struct cfw_theme_options *target_options(void)
{
    return (void *)(files[0].data + sizeof(struct pack_header) +
                    sizeof(struct cfw_theme_style));
}

/* Give an asset of the target pack a tint role and set the Theme Default tint. */
static inline void tint_target_role(unsigned asset, unsigned role, unsigned char red,
                                    unsigned char green, unsigned char blue)
{
    struct cfw_theme_options *o = target_options();
    if (asset < CFW_THEME_MAX_ASSETS)
        o->tint[asset] = role;
    o->icon[0] = red;
    o->icon[1] = green;
    o->icon[2] = blue;
    fix_crc();
}

/* Mark an asset of the target pack as tinted in the icon tint. */
static inline void tint_target(unsigned asset, unsigned char red, unsigned char green,
                               unsigned char blue)
{
    tint_target_role(asset, CFW_TINT_ICON, red, green, blue);
}
