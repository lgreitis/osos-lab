/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_support.h"

static uint32_t style[CFW_THEME_STYLE_BYTES / 4];

/* Opens the target pack and streams it into resident artwork; touched collects
 * the assets the pack wrote. */
static int apply_target(int dark, uint32_t *touched)
{
    struct cfw_theme_reader reader;
    memset(touched, 0, CFW_THEME_BITMAP_WORDS * sizeof(*touched));
    if (!cfw_theme_pack_open(&incoming, dark, &reader, (unsigned char *)style)) {
        assert(!reader.file);
        return 0;
    }
    int ok = cfw_theme_pack_apply(&reader, (unsigned char *)style, touched);
    cfw_theme_reader_close(&reader);
    cfw_catalog_reload();
    return ok;
}

static void restore_all(void)
{
    uint32_t all[CFW_THEME_BITMAP_WORDS] = {(1u << ASSETS) - 1};
    make_baseline();
    assert(cfw_theme_baseline_restore(&baseline, all));
    cfw_catalog_reload();
    assert(!memcmp(pixels, original, sizeof(pixels)));
    assert(!allocations && cfw_catalog_sources[0].data == payload_packed);
}

static void crc(void)
{
    uint32_t bitwise = UINT32_MAX;
    const unsigned char *text = (const unsigned char *)"123456789";
    assert(~cfw_theme_crc32(UINT32_MAX, text, 9) == 0xcbf43926u);
    for (unsigned i = 0; i < 9; i++) {
        bitwise ^= text[i];
        for (unsigned bit = 0; bit < 8; bit++)
            bitwise = (bitwise >> 1) ^ (0xedb88320u & (0u - (bitwise & 1u)));
    }
    assert(cfw_theme_crc32(UINT32_MAX, text, 9) == bitwise);
}

/* Boot reads only the prefixes; damaged records are found when they are applied
 * or restored. */
static void prefix_checks(void)
{
    make_baseline();
    unsigned attempts = alloc_attempts, before = read_bytes;
    assert(cfw_theme_baseline_valid(&baseline));
    assert(read_bytes - before == 52 && alloc_attempts == attempts);
    files[1].data[60] ^= 1;
    assert(cfw_theme_baseline_valid(&baseline));
    for (unsigned fault = 0; fault < 9; fault++) {
        make_baseline();
        struct pack_header *h = (void *)files[1].data;
        switch (fault) {
        case 0:
            missing_files = 2;
            break;
        case 1:
            files[1].data[4] ^= 1; /* catalog fingerprint */
            break;
        case 2: /* Well-formed but incomplete baseline. */
            make_pack(&files[1], 0, ASSETS - 1, 0);
            break;
        case 3:
            h->size--;
            break;
        case 4:
            files[1].size++; /* trailing data */
            break;
        case 7:
            files[1].size--; /* truncated */
            break;
        case 8:
            short_reads = 1;
            break;
        case 5:
            reads_fail = 1;
            break;
        case 6: /* A dark pack cannot serve as the light baseline. */
            make_pack(&files[1], 1, ASSETS, 0);
            break;
        }
        assert(!cfw_theme_baseline_valid(&baseline));
        missing_files = short_reads = reads_fail = 0;
        assert(!allocations && opens == closes);
    }
    make_baseline();

    make_target(0);
    before = read_bytes;
    assert(cfw_theme_pack_valid(&incoming, 1));
    assert(read_bytes - before == 52 + CFW_THEME_STYLE_BYTES);
    assert(!cfw_theme_pack_valid(&incoming, 0));
    files[0].data[60 + CFW_THEME_STYLE_BYTES] ^= 1;
    assert(cfw_theme_pack_valid(&incoming, 1));
    for (unsigned fault = 0; fault < 4; fault++) {
        make_target(0);
        if (fault == 0)
            missing_files = 1;
        if (fault == 1) {
            files[0].data[52 + 3] = 0; /* transparent style color */
            fix_crc();
        }
        if (fault == 2)
            short_reads = 1;
        if (fault == 3)
            files[0].size--; /* truncated */
        assert(!cfw_theme_pack_valid(&incoming, 1));
        missing_files = short_reads = 0;
        assert(alloc_attempts == attempts && opens == closes);
    }
    make_target(0);
}

/* Records stream into resident artwork without a heap copy. */
static void streaming(void)
{
    uint32_t touched[CFW_THEME_BITMAP_WORDS];
    make_target(0);
    peak_bytes = 0;
    assert(apply_target(1, touched));
    assert(touched[0] == 1 && pixels[0][32] == 99 && !peak_bytes);
    assert(!memcmp(pixels[1], original[1], sizeof(pixels[1])));
    /* The style prefix stays intact for the caller. */
    assert(!memcmp(style, files[0].data + 52, CFW_THEME_STYLE_BYTES));
    restore_all();
    /* Classic artwork uses RTH4 too. */
    make_pack(&files[0], 1, 1, 1);
    assert(apply_target(1, touched) && touched[0] == 1 && pixels[0][32] == 99);
    restore_all();
    /* Color-only packs touch nothing. */
    make_target(0);
    struct pack_header *h = (void *)files[0].data;
    h->count = 0;
    h->size = CFW_THEME_STYLE_BYTES;
    files[0].size = 52 + CFW_THEME_STYLE_BYTES;
    fix_crc();
    assert(apply_target(1, touched) && !touched[0]);
    assert(!memcmp(pixels, original, sizeof(pixels)));

    /* Damage found while streaming fails the apply. touched still names every
     * asset that may have changed; a rejected record holds zeros. */
    for (unsigned fault = 0; fault < 9; fault++) {
        make_target(0);
        unsigned char *record = files[0].data + 52 + CFW_THEME_STYLE_BYTES;
        int before_write = 0;
        switch (fault) {
        case 0:
            record[12 + 4] ^= 1; /* checksum */
            break;
        case 1: /* the size check fails before any record is read */
            files[0].size--;
            before_write = 1;
            break;
        case 2:
            files[0].size++;
            before_write = 1;
            break;
        case 3:
            ((struct record *)record)->asset = ASSETS;
            fix_crc();
            before_write = 1;
            break;
        case 4:
            record[12 + 8] = 2; /* palette index */
            fix_crc();
            break;
        case 5:
            record[23] = 1; /* padding */
            fix_crc();
            break;
        case 6: /* fails the header read */
            short_reads = 1;
            before_write = 1;
            break;
        case 7:
            tint_target(1, 1, 2, 3); /* paletted, not replaced */
            break;
        case 8: /* header, end of file, style, record header, record data */
            fail_read_at = reads + 5;
            break;
        }
        assert(!apply_target(1, touched));
        short_reads = 0;
        fail_read_at = 0;
        assert(touched[0] == (before_write ? 0u : 1u));
        if (fault == 4 || fault == 8)
            for (unsigned i = 32; i < 42; i++)
                assert(!pixels[0][i]);
        assert(!allocations && opens == closes);
        restore_all();
    }
    /* A rejected prefix opens nothing and touches nothing. */
    make_target(0);
    files[0].data[52 + 3] = 0;
    fix_crc();
    assert(!apply_target(1, touched) && !touched[0] && opens == closes);
    assert(!memcmp(pixels, original, sizeof(pixels)));
    make_target(0);
}

/* Restoration seeks to each bitmap it needs, checks it against the original
 * checksum and never reads for catalog bitmaps. */
static void restoration(void)
{
    uint32_t first[CFW_THEME_BITMAP_WORDS] = {1},
             argb[CFW_THEME_BITMAP_WORDS] = {1u << ARGB_ASSET};
    make_baseline();
    pixels[0][32] = 99;
    unsigned before = read_bytes;
    assert(cfw_theme_baseline_restore(&baseline, first));
    unsigned early = read_bytes - before;
    assert(early == 52 + 12 + 10); /* the header, then only the record */
    assert(!memcmp(pixels, original, sizeof(pixels)));
    pixels[ARGB_ASSET][28] = 99;
    before = read_bytes;
    assert(cfw_theme_baseline_restore(&baseline, argb));
    assert(read_bytes - before == 52 + 12 + 8);
    assert(!memcmp(pixels, original, sizeof(pixels)));
    uint32_t catalog[CFW_THEME_BITMAP_WORDS] = {1u << PAYLOAD_ASSET},
             none[CFW_THEME_BITMAP_WORDS] = {0};
    int opened = opens;
    assert(cfw_theme_baseline_restore(&baseline, catalog));
    assert(cfw_theme_baseline_restore(&baseline, none) && opens == opened);
    /* Records that are not requested are skipped by seeking, unread. */
    files[1].data[files[1].size - 1] ^= 1;
    files[1].data[52 + CFW_THEME_STYLE_BYTES + 24 + 12] ^= 1;
    before = read_bytes;
    assert(cfw_theme_baseline_restore(&baseline, first));
    assert(read_bytes - before == early);
    assert(cfw_theme_baseline_restore(&baseline, argb));
    /* A truncated baseline fails its size check. */
    make_baseline();
    files[1].size -= 4;
    assert(!cfw_theme_baseline_restore(&baseline, first));
    for (unsigned fault = 0; fault < 4; fault++) {
        make_baseline();
        unsigned char *record = files[1].data + 52 + CFW_THEME_STYLE_BYTES;
        if (fault == 0)
            record[12 + 4] ^= 1; /* original checksum */
        if (fault == 1)
            record[12 + 8] = 2; /* palette index */
        if (fault == 2)
            ((struct record *)record)->asset = 1;
        if (fault == 3)
            reads_fail = 1;
        assert(!cfw_theme_baseline_restore(&baseline, first));
        reads_fail = 0;
        if (fault == 1)
            for (unsigned i = 32; i < 42; i++)
                assert(!pixels[0][i]);
        assert(opens == closes);
        memcpy(pixels, original, sizeof(pixels));
    }
    cfw_catalog_reload();
    make_baseline();
}

/* A catalog bitmap replacement becomes a heap source; resident bytes and the
 * reference row stay untouched. */
static void catalog_records(void)
{
    uint32_t touched[CFW_THEME_BITMAP_WORDS];
    make_target(PAYLOAD_ASSET);
    /* A view already showing the slot sees the new art. */
    const unsigned char *shown = payload_pixels();
    assert(shown[0] == 0x12);
    assert(apply_target(1, touched) && touched[0] == 1u << PAYLOAD_ASSET);
    uint32_t index;
    assert(allocations == 1 && cfw_catalog_sources[0].data != payload_packed);
    assert(shown[0] == 99 && cfw_theme_asset_from_pixels(shown, &index) &&
           index == PAYLOAD_ASSET);
    assert(!memcmp(payload_pixels() + 1, pixels[PAYLOAD_ASSET] + 29, 7));
    assert(!memcmp(pixels, original, sizeof(pixels)));
    /* Replacing it again frees the previous copy before allocating. */
    unsigned copy = live_bytes;
    peak_bytes = live_bytes;
    assert(apply_target(1, touched) && allocations == 1 && peak_bytes == copy);
    /* Built-in art needs no copy. */
    make_pack(&files[0], 1, ASSETS, 0);
    assert(apply_target(1, touched) && !allocations);
    assert(cfw_catalog_sources[0].data == payload_packed);
    /* Restoration returns the built-in source. */
    make_target(PAYLOAD_ASSET);
    assert(apply_target(1, touched) && allocations == 1);
    restore_all();
    /* A failed heap copy fails the apply with nothing allocated. */
    make_target(PAYLOAD_ASSET);
    fail_alloc = 1;
    assert(!apply_target(1, touched) && touched[0] == 1u << PAYLOAD_ASSET);
    fail_alloc = 0;
    assert(!allocations && cfw_catalog_sources[0].data == payload_packed);
    restore_all();
    assert(payload_pixels()[0] == 0x12);
    make_target(0);
}

/* Tint roles: masks may be tinted without a record, paletted and ARGB assets only
 * with one, nothing outside the catalog, and no unknown role. */
static void tinting(void)
{
    uint32_t touched[CFW_THEME_BITMAP_WORDS];
    make_target(0);
    tint_target(MASK_ASSET, 1, 2, 3);
    assert(apply_target(1, touched));
    const struct cfw_theme_options *o =
        (const void *)((unsigned char *)style + sizeof(struct cfw_theme_style));
    assert(o->tint[MASK_ASSET] == CFW_TINT_ICON && o->icon[2] == 3);
    restore_all();
    make_target(0);
    tint_target(0, 1, 2, 3); /* paletted, replaced by this pack */
    assert(apply_target(1, touched));
    restore_all();
    make_target(ARGB_ASSET);
    tint_target_role(ARGB_ASSET, CFW_TINT_ROLE_BASE + CFW_THEME_SELECTION, 1, 2, 3);
    assert(apply_target(1, touched));
    restore_all();
    make_target(0);
    tint_target_role(ARGB_ASSET, CFW_TINT_ROLE_BASE + CFW_THEME_SELECTION, 1, 2, 3);
    assert(!apply_target(1, touched));
    restore_all();
    make_target(MASK_ASSET);
    tint_target_role(MASK_ASSET, CFW_TINT_COUNT, 1, 2, 3); /* unknown role */
    assert(!cfw_theme_pack_valid(&incoming, 1));
    make_target(0);
    tint_target(ASSETS, 1, 2, 3);
    assert(!apply_target(1, touched));
    restore_all();
    make_target(0);
    tint_target(CFW_THEME_MAX_ASSETS - 1, 1, 2, 3);
    assert(!apply_target(1, touched));
    restore_all();
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
    (void)active_bits;
    crc();
    prefix_checks();
    streaming();
    restoration();
    catalog_records();
    tinting();
    catalog_codec();
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
