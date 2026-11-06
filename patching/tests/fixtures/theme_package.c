/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_support.h"

static void rejection(void)
{
    struct cfw_theme_pack p;
    unsigned char before[sizeof(pixels)];
    memcpy(before, pixels, sizeof(pixels));
    assert(!cfw_theme_pack_read(&incoming, 1, 0, &p));
    assert(!p.data && !allocations && !memcmp(before, pixels, sizeof(pixels)));
    assert(opens == closes);
}

static void baseline_validation(void)
{
    make_baseline();
    unsigned attempts = alloc_attempts;
    assert(cfw_theme_baseline_valid(&baseline));
    assert(alloc_attempts == attempts && !allocations);
    for (unsigned fault = 0; fault < 9; fault++) {
        make_baseline();
        switch (fault) {
        case 0:
            missing_files = 2;
            break;
        case 1:
            files[1].size--;
            break;
        case 2:
            files[1].data[4] ^= 1;
            break;
        case 3:
            files[1].data[60] ^= 1;
            break;
        case 4: /* Well-formed but incomplete baseline. */
            make_pack(&files[1], 0, ASSETS - 1, 0);
            break;
        case 5:
            files[1].size++;
            break;
        case 6:
            short_reads = 1;
            break;
        case 7:
            reads_fail = 1;
            break;
        case 8: /* A dark pack cannot serve as the light baseline. */
            make_pack(&files[1], 1, ASSETS, 0);
            break;
        }
        assert(!cfw_theme_baseline_valid(&baseline));
        missing_files = short_reads = reads_fail = 0;
        assert(!allocations && opens == closes);
    }
    make_baseline();
}

/* The boot probe streams a pack without allocating and rejects the same faults
 * as a full read, except palette indices, which the read checks pixel by pixel. */
static void pack_probe(void)
{
    make_target(0);
    unsigned attempts = alloc_attempts;
    assert(cfw_theme_pack_valid(&incoming, 1));
    assert(!cfw_theme_pack_valid(&incoming, 0));
    assert(alloc_attempts == attempts && !allocations && opens == closes);
    for (unsigned fault = 0; fault < 7; fault++) {
        make_target(0);
        switch (fault) {
        case 0:
            missing_files = 1;
            break;
        case 1:
            files[0].size--;
            break;
        case 2:
            files[0].size++;
            break;
        case 3:
            files[0].data[60] ^= 1;
            break;
        case 4: /* Transparent style color with a matching checksum. */
            files[0].data[52 + 3] = 0;
            fix_crc();
            break;
        case 5:
            ((struct record *)(files[0].data + 52 + CFW_THEME_STYLE_BYTES))->asset =
                ASSETS;
            fix_crc();
            break;
        case 6:
            short_reads = 1;
            break;
        }
        assert(!cfw_theme_pack_valid(&incoming, 1));
        missing_files = short_reads = 0;
        assert(alloc_attempts == attempts && opens == closes);
    }
    make_target(0);
}

/* Tint roles: masks may be tinted without a record, paletted and ARGB assets only
 * with one, nothing outside the catalog, and no unknown role. Both the reader and
 * the probe agree. */
static void tinting(void)
{
    struct cfw_theme_pack p;
    make_target(0);
    tint_target(MASK_ASSET, 1, 2, 3);
    assert(cfw_theme_pack_valid(&incoming, 1));
    assert(cfw_theme_pack_read(&incoming, 1, 0, &p));
    const struct cfw_theme_options *o =
        (const void *)(p.data + sizeof(struct cfw_theme_style));
    assert(o->tint[MASK_ASSET] == CFW_TINT_ICON && o->icon[2] == 3);
    cfw_theme_pack_release(&p);
    make_target(0);
    tint_target(0, 1, 2, 3); /* paletted, replaced by this pack */
    assert(cfw_theme_pack_valid(&incoming, 1));
    assert(cfw_theme_pack_read(&incoming, 1, 0, &p));
    cfw_theme_pack_release(&p);
    make_target(0);
    tint_target(1, 1, 2, 3); /* paletted, not replaced */
    assert(!cfw_theme_pack_valid(&incoming, 1));
    rejection();
    make_target(ARGB_ASSET);
    tint_target_role(ARGB_ASSET, CFW_TINT_ROLE_BASE + CFW_THEME_SELECTION, 1, 2, 3);
    assert(cfw_theme_pack_valid(&incoming, 1));
    assert(cfw_theme_pack_read(&incoming, 1, 0, &p));
    cfw_theme_pack_release(&p);
    make_target(0);
    tint_target_role(ARGB_ASSET, CFW_TINT_ROLE_BASE + CFW_THEME_SELECTION, 1, 2, 3);
    assert(!cfw_theme_pack_valid(&incoming, 1));
    rejection();
    make_target(MASK_ASSET);
    tint_target_role(MASK_ASSET, CFW_TINT_COUNT, 1, 2, 3); /* unknown role */
    assert(!cfw_theme_pack_valid(&incoming, 1));
    rejection();
    make_target(0);
    tint_target(ASSETS, 1, 2, 3);
    assert(!cfw_theme_pack_valid(&incoming, 1));
    rejection();
    make_target(0);
    tint_target(CFW_THEME_MAX_ASSETS - 1, 1, 2, 3);
    rejection();
    assert(!allocations && opens == closes);

    uint32_t index = 99, colors = 0;
    assert(cfw_theme_asset_from_pixels(pixels[1] + 28, &index) && index == 1);
    assert(cfw_theme_asset_from_pixels(pixels[MASK_ASSET] + 28, &index) &&
           index == MASK_ASSET);
    /* The payload asset is found in the slot, not in its unpacked reference row. */
    assert(!cfw_theme_asset_from_pixels(pixels[PAYLOAD_ASSET] + 28, &index));
    assert(cfw_theme_asset_from_pixels(payload_pixels(), &index) &&
           index == PAYLOAD_ASSET);
    assert(!cfw_theme_asset_from_pixels(payload_pixels() + 1, &index));
    assert(!cfw_theme_asset_from_pixels(cfw_catalog_slot, &index));
    assert(!cfw_theme_asset_palette(PAYLOAD_ASSET, &colors));
    assert(!cfw_theme_asset_from_pixels(pixels[1] + 29, &index));
    assert(!cfw_theme_asset_from_pixels(pixels[1] + 32, &index));
    assert(!cfw_theme_asset_from_pixels(pixels[0], &index));
    assert(!cfw_theme_asset_from_pixels(pixels[ASSETS - 1] + 44, &index));
    assert(!cfw_theme_asset_from_pixels(NULL, &index));
    assert(cfw_theme_asset_palette(0, &colors) == pixels[0] + 32 && colors == 2);
    assert(!cfw_theme_asset_palette(MASK_ASSET, &colors));
    assert(!cfw_theme_asset_palette(ASSETS, &colors));
    assert(!cfw_theme_asset_palette(ARGB_ASSET, &colors));
    uint32_t width = 0, height = 0, stride = 0;
    assert(cfw_theme_asset_argb(ARGB_ASSET, &width, &height, &stride) ==
           pixels[ARGB_ASSET] + 28);
    assert(width == 2 && height == 1 && stride == 8);
    assert(!cfw_theme_asset_argb(0, &width, &height, &stride));
    assert(!cfw_theme_asset_argb(MASK_ASSET, &width, &height, &stride));
    assert(!cfw_theme_asset_argb(ASSETS, &width, &height, &stride));
    /* Center pixels round toward the origin: the 2x1 paletted asset's center is
     * its first pixel, palette entry 0 (RGB 1,2,3 from BGRA 3,2,1); the 2x1 ARGB
     * asset's center is its first, opaque pixel; masks and out-of-range indices
     * have none. */
    unsigned char center[4] = {0};
    assert(cfw_theme_asset_center_pixel(0, center) && center[0] == 1 &&
           center[1] == 2 && center[2] == 3 && center[3] == 255);
    assert(cfw_theme_asset_center_pixel(ARGB_ASSET, center) && center[0] == 20 &&
           center[1] == 30 && center[2] == 40 && center[3] == 255);
    assert(!cfw_theme_asset_center_pixel(MASK_ASSET, center));
    assert(!cfw_theme_asset_center_pixel(ASSETS, center));
    pixels[0][32 + 3] = 0; /* a transparent palette entry */
    assert(!cfw_theme_asset_center_pixel(0, center));
    pixels[0][32 + 3] = 255;
    pixels[0][40] = 2; /* a pixel index past the palette */
    assert(!cfw_theme_asset_center_pixel(0, center));
    pixels[0][40] = 0;
    pixels[ARGB_ASSET][31] = 0;
    assert(!cfw_theme_asset_center_pixel(ARGB_ASSET, center));
    pixels[ARGB_ASSET][31] = 255;
    make_target(0);
}

/* Packed sources decode into the slot, and malformed ones are refused. */
static void catalog_codec(void)
{
    unsigned char packed[16];
    assert(cfw_catalog_pack(pixels[PAYLOAD_ASSET] + 28, NULL) ==
           sizeof(payload_packed));
    assert(cfw_catalog_pack(pixels[PAYLOAD_ASSET] + 28, packed) ==
           sizeof(payload_packed));
    assert(!memcmp(packed, payload_packed, sizeof(payload_packed)));
    assert(cfw_catalog_bitmap(0) == cfw_catalog_slot);
    assert(!memcmp(cfw_catalog_slot, pixels[PAYLOAD_ASSET], 36));
    assert(!cfw_catalog_bitmap(1));
    static const unsigned char flat[8] = {7, 7, 7, 7, 7, 7, 7, 7},
                               mixed[8] = {1, 2, 0, 3, 0, 0, 4, 4};
    assert(cfw_catalog_pack(flat, packed) == 2 && packed[0] == 0x87 && packed[1] == 7);
    /* A lone zero and a pair of equal bytes stay literal; two zeros do not. */
    assert(cfw_catalog_pack(mixed, packed) == 9 && packed[0] == 0x03 &&
           packed[5] == 0x41 && packed[6] == 0x01 && packed[8] == 4);
    static const unsigned char bad[][3] = {
        {0xc0, 0, 0},    /* unknown run kind */
        {0x02, 1, 2},    /* literals past the end */
        {0x88, 0, 0},    /* repeat without its byte */
        {0x47, 0x40, 0}, /* runs past the pixel count */
        {0x46, 0, 0},    /* too few pixels */
    };
    static const unsigned bad_sizes[] = {1, 3, 1, 2, 1};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        cfw_catalog_sources[0] = (struct cfw_catalog_source){bad[i], bad_sizes[i]};
        cfw_catalog_reload();
        assert(!cfw_catalog_bitmap(0));
        uint32_t index;
        assert(!cfw_theme_asset_from_pixels(cfw_catalog_slot + 28, &index));
    }
    cfw_catalog_sources[0] = cfw_catalog_builtin[0];
    cfw_catalog_reload();
    assert(!memcmp(payload_pixels(), pixels[PAYLOAD_ASSET] + 28, 8));
}

/* A catalog bitmap replacement becomes a heap source that the swap exchanges with
 * the live one; resident bytes and the reference row stay untouched. */
static void catalog_swap(void)
{
    struct cfw_theme_pack pack;
    struct cfw_theme_transaction tx;
    make_baseline();
    make_target(PAYLOAD_ASSET);
    assert(cfw_theme_pack_read(&incoming, 1, 0, &pack));
    assert(cfw_theme_transaction_prepare(&pack, 1, active_bits, &baseline, &tx));
    cfw_theme_pack_release(&pack);
    assert(allocations == 2 && !tx.patch.data && tx.source_mask == 1);
    /* A view already showing the slot sees the new art and keeps its tint. */
    const unsigned char *shown = payload_pixels();
    assert(shown[0] == 0x12);
    cfw_theme_transaction_swap(&tx);
    uint32_t index;
    assert(shown[0] == 99 && cfw_theme_asset_from_pixels(shown, &index) &&
           index == PAYLOAD_ASSET);
    assert(cfw_catalog_sources[0].data != payload_packed);
    assert(payload_pixels()[0] == 99 &&
           !memcmp(payload_pixels() + 1, pixels[PAYLOAD_ASSET] + 29, 7));
    cfw_theme_transaction_swap(&tx); /* rollback */
    assert(payload_pixels()[0] == 0x12);
    cfw_theme_transaction_swap(&tx);
    memcpy(active_bits, tx.target_assets, sizeof(active_bits));
    cfw_theme_transaction_release(&tx);
    /* The live heap source belongs to the active theme now. */
    assert(allocations == 1 && payload_pixels()[0] == 99);
    assert(!memcmp(pixels, original, sizeof(pixels)));

    /* Replacing it again frees the old heap source once the new one is live. */
    make_target(PAYLOAD_ASSET);
    assert(cfw_theme_pack_read(&incoming, 1, 0, &pack));
    assert(cfw_theme_transaction_prepare(&pack, 1, active_bits, &baseline, &tx));
    cfw_theme_pack_release(&pack);
    const unsigned char *previous = cfw_catalog_sources[0].data;
    cfw_theme_transaction_swap(&tx);
    assert(tx.sources[0].data == previous);
    memcpy(active_bits, tx.target_assets, sizeof(active_bits));
    cfw_theme_transaction_release(&tx);
    assert(allocations == 1 && payload_pixels()[0] == 99);

    /* Restoring stock returns the built-in source without reading its baseline
     * record; an aborted transaction frees only what it allocated. */
    assert(cfw_theme_transaction_prepare(NULL, 0, active_bits, &baseline, &tx));
    assert(!tx.patch.data && tx.sources[0].data == payload_packed);
    cfw_theme_transaction_release(&tx);
    assert(allocations == 1);
    assert(cfw_theme_transaction_prepare(NULL, 0, active_bits, &baseline, &tx));
    cfw_theme_transaction_swap(&tx);
    cfw_theme_transaction_release(&tx);
    memset(active_bits, 0, sizeof(active_bits));
    assert(!allocations && cfw_catalog_sources[0].data == payload_packed);
    assert(payload_pixels()[0] == 0x12);

    /* Built-in art in a pack needs no heap copy. */
    make_pack(&files[0], 1, ASSETS, 0);
    assert(cfw_theme_pack_read(&incoming, 1, 1, &pack));
    assert(cfw_theme_transaction_prepare(&pack, 1, active_bits, &baseline, &tx));
    cfw_theme_pack_release(&pack);
    assert(allocations == 1 && tx.sources[0].data == payload_packed);
    cfw_theme_transaction_release(&tx);

    /* A failed heap copy fails the prepare and leaves nothing allocated. */
    make_target(PAYLOAD_ASSET);
    assert(cfw_theme_pack_read(&incoming, 1, 0, &pack));
    fail_alloc_at = alloc_attempts + 1;
    assert(!cfw_theme_transaction_prepare(&pack, 1, active_bits, &baseline, &tx));
    fail_alloc_at = 0;
    cfw_theme_pack_release(&pack);
    assert(!allocations && cfw_catalog_sources[0].data == payload_packed);
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
    baseline_validation();
    pack_probe();
    tinting();
    catalog_codec();
    catalog_swap();
    make_baseline();
    struct cfw_theme_pack pack;
    struct cfw_theme_transaction tx;
    for (unsigned cycle = 0; cycle < 100; cycle++) {
        unsigned asset = cycle % 2;
        make_target(asset);
        peak_bytes = 0;
        assert(cfw_theme_pack_read(&incoming, 1, 0, &pack));
        unsigned expected_peak = pack.size + (cycle ? 24 : 0);
        assert(cfw_theme_transaction_prepare(&pack, 1, active_bits, &baseline, &tx));
        assert(!pack.data && peak_bytes == expected_peak);
        cfw_theme_pack_release(&pack);
        unsigned char before[sizeof(pixels)];
        memcpy(before, pixels, sizeof(pixels));
        cfw_theme_transaction_swap(&tx);
        assert(pixels[asset][32] == 99);
        assert(!memcmp(pixels[1 - asset], original[1 - asset], 42));
        assert(!memcmp(pixels[MASK_ASSET], original[MASK_ASSET], 42));
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
    /* Classic artwork uses RTH4 but keeps the built-in style. */
    make_pack(&files[0], 1, 1, 1);
    assert(cfw_theme_pack_read(&incoming, 1, 1, &pack));
    assert(pack.records_offset == CFW_THEME_STYLE_BYTES && pack.count == 1 &&
           !pack.custom);
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
    ((struct record *)(files[0].data + 52 + CFW_THEME_STYLE_BYTES))->asset = ASSETS;
    fix_crc();
    rejection();
    make_target(0);
    files[0].data[52 + CFW_THEME_STYLE_BYTES + 12 + 8] = 2;
    fix_crc();
    rejection(); /* palette index */
    make_target(0);
    files[0].data[52 + 3] = 0;
    fix_crc();
    rejection(); /* opacity */
    make_target(0);
    files[0].data[52 + CFW_THEME_STYLE_BYTES + 23] = 1;
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
    active_bits[0] = 2; /* A disjoint previous replacement needs restoration. */
    fail_alloc = 1;
    assert(!cfw_theme_transaction_prepare(&pack, 1, active_bits, &baseline, &tx));
    fail_alloc = 0;
    active_bits[0] = 0;
    assert(pack.data); /* Failure does not consume the incoming allocation. */
    cfw_theme_pack_release(&pack);
    assert(!allocations);
    /* No-op/custom color-only packages require no bitmap allocation. */
    make_target(0);
    struct pack_header *h = (void *)files[0].data;
    h->count = 0;
    h->size = CFW_THEME_STYLE_BYTES;
    files[0].size = 52 + CFW_THEME_STYLE_BYTES;
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
    const char *reserved[] = {"default", "dark", "classic"};
    for (unsigned i = 0; i < sizeof(reserved) / sizeof(*reserved); i++) {
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
