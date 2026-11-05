/* SPDX-License-Identifier: GPL-3.0-only */
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
    unsigned char data[256];
    unsigned size, cursor;
} files[3];

static int fail_alloc, allocations, reads_fail, short_reads, opens, closes;

static void *osos_import_alloc(uint32_t size)
{
    if (fail_alloc)
        return NULL;
    allocations++;
    return malloc(size);
}

static void osos_import_free(void *data)
{
    allocations--;
    free(data);
}

static int osos_file_open(const struct osos_path *p, uint32_t mode, void **out)
{
    assert(mode == 1);
    unsigned n = p->text[0] - '0';
    assert(n < 3);
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

static void make_target(unsigned asset)
{
    memset(&files[0], 0, sizeof(files[0]));
    struct pack_header *h = (void *)files[0].data;
    h->magic = 0x34485452;
    memcpy(h->catalog, cfw_theme_catalog_hash, 32);
    h->appearance = 1;
    h->size = 88;
    h->count = 1;
    unsigned char *body = files[0].data + sizeof(*h);
    for (unsigned i = 0; i < 16; i++)
        body[i * 4 + 3] = 255;
    write_record(body + 64, asset, 1);
    h->crc = ~crc_update(UINT32_MAX, body, h->size);
    files[0].size = sizeof(*h) + h->size;
}

static void make_baseline(void)
{
    struct file *f = &files[1];
    memset(f, 0, sizeof(*f));
    uint32_t *h = (void *)f->data;
    h[0] = 0x33485452;
    memcpy(f->data + 4, cfw_theme_catalog_hash, 32);
    h[9] = 2;
    h[10] = 48;
    write_record(f->data + 48, 0, 0);
    write_record(f->data + 72, 1, 0);
    h[11] = ~crc_update(UINT32_MAX, f->data + 48, 48);
    f->size = 96;
}

static void fix_crc(void)
{
    struct pack_header *h = (void *)files[0].data;
    h->crc = ~crc_update(UINT32_MAX, files[0].data + sizeof(*h), h->size);
}

static void rejection(void)
{
    struct cfw_theme_pack p;
    unsigned char before[sizeof(pixels)];
    memcpy(before, pixels, sizeof(pixels));
    assert(!cfw_theme_pack_read(&incoming, 1, 0, &p));
    assert(!p.data && !allocations && !memcmp(before, pixels, sizeof(pixels)));
    assert(opens == closes);
}

int main(void)
{
    _Static_assert(sizeof(struct pack_header) == 52, "RTH4 header");
    _Static_assert(sizeof(struct cfw_theme_metadata) == 120, "RTM4 header");
    assert(metadata_name((const unsigned char *)"Caf\xc3\xa9", 6));
    assert(!metadata_name((const unsigned char *)"\xc0\xaf", 3));
    assert(!metadata_name((const unsigned char *)"\xed\xa0\x80", 4));
    assert(!metadata_name((const unsigned char *)"\xf4\x90\x80\x80", 5));
    assert(!metadata_name((const unsigned char *)"A\0B", 4));
    assert(!metadata_name((const unsigned char *)"A\xe2", 2));
    memcpy(original, pixels, sizeof(pixels));
    make_baseline();
    struct cfw_theme_pack pack;
    struct cfw_theme_transaction tx;
    for (unsigned cycle = 0; cycle < 100; cycle++) {
        unsigned asset = cycle % 2;
        make_target(asset);
        assert(cfw_theme_pack_read(&incoming, 1, 0, &pack));
        assert(cfw_theme_transaction_prepare(&pack, 1, active_bits, &baseline, &tx));
        cfw_theme_pack_release(&pack);
        unsigned char before[sizeof(pixels)];
        memcpy(before, pixels, sizeof(pixels));
        cfw_theme_transaction_swap(&tx);
        assert(pixels[asset][32] == 99);
        assert(!memcmp(pixels[1 - asset], original[1 - asset], 42));
        cfw_theme_transaction_swap(&tx); /* explicit rollback */
        assert(!memcmp(pixels, before, sizeof(pixels)));
        cfw_theme_transaction_swap(&tx);
        memcpy(active_bits, tx.target_assets, sizeof(active_bits));
        cfw_theme_transaction_release(&tx);
        assert(!allocations && opens == closes);
    }
    assert(cfw_theme_transaction_prepare(NULL, 0, active_bits, &baseline, &tx));
    cfw_theme_transaction_swap(&tx);
    cfw_theme_transaction_release(&tx);
    memset(active_bits, 0, sizeof(active_bits));
    assert(!memcmp(pixels, original, sizeof(pixels)));
    /* Legacy RTH3 artwork uses the same prepare/swap/restore contract. */
    make_target(0);
    memmove(files[0].data + 48, files[0].data + 52 + 64, 24);
    uint32_t *legacy_header = (void *)files[0].data;
    legacy_header[0] = RTH3_MAGIC;
    legacy_header[9] = 1;
    legacy_header[10] = 24;
    legacy_header[11] = ~crc_update(UINT32_MAX, files[0].data + 48, 24);
    files[0].size = 72;
    assert(cfw_theme_pack_read(&incoming, 1, 1, &pack));
    assert(!pack.records_offset && pack.count == 1);
    assert(cfw_theme_transaction_prepare(&pack, 1, active_bits, &baseline, &tx));
    cfw_theme_pack_release(&pack);
    assert(!tx.custom && tx.dark && !tx.applied);
    cfw_theme_transaction_swap(&tx);
    assert(tx.applied && pixels[0][32] == 99);
    cfw_theme_transaction_swap(&tx);
    assert(!tx.applied && !memcmp(pixels, original, sizeof(pixels)));
    cfw_theme_transaction_swap(&tx);
    memcpy(active_bits, tx.target_assets, sizeof(active_bits));
    cfw_theme_transaction_release(&tx);
    assert(cfw_theme_transaction_prepare(NULL, 0, active_bits, &baseline, &tx));
    assert(!tx.custom && !tx.dark && !tx.target_assets[0]);
    cfw_theme_transaction_swap(&tx);
    cfw_theme_transaction_release(&tx);
    memset(active_bits, 0, sizeof(active_bits));
    assert(!allocations && !memcmp(pixels, original, sizeof(pixels)));
    make_target(0);
    files[0].data[60] ^= 1;
    rejection();
    make_target(0);
    files[0].size--;
    rejection();
    make_target(0);
    files[0].size++;
    rejection();
    make_target(0);
    ((struct record *)(files[0].data + 52 + 64))->asset = 2;
    fix_crc();
    rejection();
    make_target(0);
    files[0].data[52 + 64 + 12 + 8] = 2;
    fix_crc();
    rejection(); /* palette index */
    make_target(0);
    files[0].data[52 + 3] = 0;
    fix_crc();
    rejection(); /* opacity */
    make_target(0);
    files[0].data[52 + 64 + 23] = 1;
    fix_crc();
    rejection(); /* padding */
    make_target(0);
    fail_alloc = 1;
    rejection();
    fail_alloc = 0;
    make_target(0);
    short_reads = 1;
    rejection();
    short_reads = 0;
    make_target(0);
    reads_fail = 1;
    rejection();
    reads_fail = 0;
    make_target(0);
    assert(cfw_theme_pack_read(&incoming, 1, 0, &pack));
    fail_alloc = 1;
    assert(!cfw_theme_transaction_prepare(&pack, 1, active_bits, &baseline, &tx));
    fail_alloc = 0;
    cfw_theme_pack_release(&pack);
    assert(!allocations);
    /* No-op/custom color-only packages require no bitmap allocation. */
    make_target(0);
    struct pack_header *h = (void *)files[0].data;
    h->count = 0;
    h->size = 64;
    files[0].size = 52 + 64;
    fix_crc();
    assert(cfw_theme_pack_read(&incoming, 1, 0, &pack));
    assert(cfw_theme_transaction_prepare(&pack, 1, active_bits, &baseline, &tx));
    assert(!tx.patch.data && tx.custom);
    cfw_theme_pack_release(&pack);
    cfw_theme_transaction_release(&tx);
    /* Interrupted/truncated baseline restoration must not change current pixels. */
    active_bits[0] = 1;
    files[1].size--;
    assert(!cfw_theme_transaction_prepare(NULL, 0, active_bits, &baseline, &tx));
    assert(!allocations && !memcmp(pixels, original, sizeof(pixels)));
    struct cfw_theme_metadata m = {
        .magic = 0x344d5452, .appearance_mask = 3, .id = "modern", .name = "Modern"};
    memcpy(m.catalog, cfw_theme_catalog_hash, 32);
    memcpy(files[2].data, &m, sizeof(m));
    files[2].size = sizeof(m);
    assert(cfw_theme_metadata_read(&metadata, &m));
    const char *reserved[] = {"default", "dark"};
    for (unsigned i = 0; i < 2; i++) {
        struct cfw_theme_metadata *stored = (void *)files[2].data;
        memset(stored->id, 0, sizeof(stored->id));
        strcpy(stored->id, reserved[i]);
        assert(!cfw_theme_metadata_read(&metadata, &m));
        assert(!strcmp(m.id, "modern"));
    }
    ((struct cfw_theme_metadata *)files[2].data)->id[0] = '.';
    assert(!cfw_theme_metadata_read(&metadata, &m));
    assert(opens == closes);
    return 0;
}
