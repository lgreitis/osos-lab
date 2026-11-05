/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define CFW_OSOS_H
#include "../../../payload/assets.h"
#include "../../../payload/theme_package.h"

static unsigned char pixels[2][42] = {
    {100, 0, 0, 0,  2, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0,   0, 1, 0, 0,   0, 2,
     0,   0, 0, 14, 0, 0, 0, 2, 0, 0, 0, 3, 2, 1, 255, 6, 5, 4, 255, 0, 1},
    {100, 0, 0, 0,  2, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0,   0, 1, 0, 0,   0, 2,
     0,   0, 0, 14, 0, 0, 0, 2, 0, 0, 0, 3, 2, 1, 255, 6, 5, 4, 255, 0, 1}};
static unsigned char original[2][42];
const struct cfw_bitmap_asset cfw_bitmap_assets[] = {{pixels[0], 42, 0x25789516},
                                                     {pixels[1], 42, 0x25789516}};
const uint32_t cfw_bitmap_asset_count = 2, cfw_theme_max_bytes = 48;
const unsigned char cfw_theme_catalog_hash[32] = {1, 2, 3};

struct osos_path {
    uint16_t volume;
    char text[258];
};

static struct file {
    unsigned char data[1024];
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

static void write_record(unsigned char *out, unsigned asset, int recolor)
{
    struct record *r = (void *)out;
    r->asset = asset;
    r->offset = 32;
    r->size = 10;
    memcpy(r->data, original[asset] + 32, 10);
    if (recolor)
        r->data[0] = 99;
    r->data[10] = r->data[11] = 0;
}

static void opaque_style(unsigned char *body)
{
    for (unsigned i = 0; i < CFW_THEME_COLOR_COUNT; i++)
        body[i * 4 + 3] = 255;
    for (unsigned i = sizeof(struct cfw_theme_style) + 7; i < CFW_THEME_STYLE_BYTES; i += 4)
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
    h->size = CFW_THEME_STYLE_BYTES + 24 * count;
    h->count = count;
    unsigned char *body = f->data + sizeof(*h);
    opaque_style(body);
    for (unsigned i = 0; i < count; i++)
        write_record(body + CFW_THEME_STYLE_BYTES + 24 * i, i, recolor);
    h->crc = ~crc_update(UINT32_MAX, body, h->size);
    f->size = sizeof(*h) + h->size;
}

static void make_target(unsigned asset)
{
    make_pack(&files[0], 1, 0, 0);
    struct pack_header *h = (void *)files[0].data;
    unsigned char *body = files[0].data + sizeof(*h);
    h->size = CFW_THEME_STYLE_BYTES + 24;
    h->count = 1;
    write_record(body + CFW_THEME_STYLE_BYTES, asset, 1);
    h->crc = ~crc_update(UINT32_MAX, body, h->size);
    files[0].size = sizeof(*h) + h->size;
}

static void make_baseline(void)
{
    make_pack(&files[1], 0, 2, 0);
}

static void fix_crc(void)
{
    struct pack_header *h = (void *)files[0].data;
    h->crc = ~crc_update(UINT32_MAX, files[0].data + sizeof(*h), h->size);
}
