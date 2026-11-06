/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_package.h"
#include "assets.h"
#include "catalog_bitmaps.h"
#include "cfw.h"
#include "osos.h"
#include "patch.h"
#include "theme_id.h"

enum { RTH4_MAGIC = 0x34485452u, RTM4_MAGIC = 0x344d5452u };

struct pack_header {
    uint32_t magic;
    unsigned char catalog[32];
    uint32_t appearance, size, count, crc;
};

_Static_assert(sizeof(struct pack_header) == 52, "RTH4 header layout changed");

struct record {
    uint32_t asset, offset, size;
    unsigned char data[];
};

struct layout {
    uint32_t start, pixels, colors, stride, width, height, depth, mask;
};

/* Resident firmware bitmap addresses follow from the catalog sizes; walk them in
 * order from cfw_bitmap_assets_base. The payload's catalog bitmaps, listed after
 * them, resolve to their shared header: their pixels are run-packed, so their
 * records install sources (catalog_bitmaps.h) instead of bytes. */
struct cursor {
    uint32_t index;
    unsigned char *data;
};

static void cursor_init(struct cursor *c)
{
    c->index = 0;
    c->data = cfw_bitmap_assets_base;
}

static int packed(uint32_t index)
{
    return index >= cfw_bitmap_firmware_count;
}

static unsigned char *asset_data(struct cursor *c, uint32_t index)
{
    if (packed(index))
        return (unsigned char *)cfw_catalog_header;
    if (index < c->index)
        cursor_init(c);
    for (; c->index < index; c->index++)
        c->data += (cfw_bitmap_assets[c->index].size + 3u) & ~3u;
    return c->data;
}

/* Reflected CRC-32 (0xedb88320) four bits at a time. */
static const uint32_t crc_nibbles[16] = {
    0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu, 0x76dc4190u, 0x6b6b51f4u,
    0x4db26158u, 0x5005713cu, 0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
    0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu,
};

/* ARM code: shifted operands make each step three instructions, against five
 * in Thumb, and packs are checksummed byte for byte on every switch. */
PATCH_ARM uint32_t cfw_theme_crc32(uint32_t crc, const void *data, uint32_t size)
{
    const unsigned char *p = data;
    while (size--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ crc_nibbles[crc & 15u];
        crc = (crc >> 4) ^ crc_nibbles[crc & 15u];
    }
    return crc;
}

static uint32_t u16(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8;
}

uint32_t cfw_theme_u32(const unsigned char *p)
{
    return u16(p) | u16(p + 2) << 16;
}

void cfw_theme_path(struct osos_path *out, const char *text)
{
    out->volume = 0;
    unsigned i = 0;
    do {
        out->text[i] = text[i];
    } while (text[i++]);
}

static int fingerprint(const unsigned char *hash)
{
    for (unsigned i = 0; i < 32; i++)
        if (hash[i] != cfw_theme_catalog_hash[i])
            return 0;
    return 1;
}

static int exact(void *handle, void *data, uint32_t bytes)
{
    uint32_t count = 0;
    return !osos_file_read(handle, data, bytes, &count) && count == bytes;
}

static int eof(void *handle)
{
    unsigned char byte;
    uint32_t count = 0;
    int result = osos_file_read(handle, &byte, 1, &count);
    /* Native FileIO reports -39 when a read starts at the end of the file. */
    return (result == 0 || result == -39) && !count;
}

static int bitmap_layout(struct cursor *c, uint32_t index, struct layout *b)
{
    if (index >= cfw_bitmap_asset_count || index >= CFW_THEME_MAX_ASSETS)
        return 0;
    uint32_t size = cfw_bitmap_assets[index].size;
    const unsigned char *p = asset_data(c, index);
    if (size < 28)
        return 0;
    b->start = cfw_theme_u32(p + 24) == size - 28 ? 28 : 32;
    if (size < b->start || cfw_theme_u32(p + b->start - 4) != size - b->start)
        return 0;
    uint32_t format = u16(p);
    b->mask = format == 4 || format == 8;
    b->depth = format == 4                         ? 4
               : format == 8 || format == 0x64     ? 8
               : format == 0x65 || format == 0x565 ? 16
               : format == 0x1888                  ? 32
                                                   : 0;
    b->stride = u16(p + 4);
    b->height = cfw_theme_u32(p + 16);
    b->width = cfw_theme_u32(p + 20);
    b->colors = 0;
    if (!b->depth || u16(p + 6) != b->depth || !b->width || !b->height || !b->stride ||
        b->width > b->stride * 8 / b->depth)
        return 0;
    if (format == 0x64 || format == 0x65) {
        if (size - b->start < 4)
            return 0;
        b->colors = cfw_theme_u32(p + b->start);
        b->start += 4;
        if (!b->colors || b->colors > (1u << b->depth) ||
            b->colors > (size - b->start) / 4)
            return 0;
    }
    b->pixels = b->start + b->colors * 4;
    return b->height <= (size - b->pixels) / b->stride &&
           b->pixels + b->height * b->stride == size;
}

/* Records are sorted by asset and replace exactly the pixel bytes of a bitmap. */
static int record_valid(struct cursor *c, const struct record *r, uint32_t i,
                        uint32_t previous, struct layout *b)
{
    return (!i || r->asset > previous) && bitmap_layout(c, r->asset, b) &&
           r->offset == b->start &&
           r->size == cfw_bitmap_assets[r->asset].size - b->start;
}

static int bit(const uint32_t *bits, unsigned index)
{
    return !!(bits[index / 32] & (1u << (index % 32)));
}

/* One bit per tinted asset; the reader clears each bit as its record arrives
 * and then requires the remaining assets to be masks, which are colored while
 * drawing. Paletted and ARGB ones need a record, so the rewritten bytes are
 * restored with it. */
static void tint_pending(const unsigned char *style, uint32_t *pending)
{
    const struct cfw_theme_options *o =
        (const void *)(style + sizeof(struct cfw_theme_style));
    for (unsigned i = 0; i < CFW_THEME_BITMAP_WORDS; i++)
        pending[i] = 0;
    for (unsigned i = 0; i < CFW_THEME_MAX_ASSETS; i++)
        if (o->tint[i])
            pending[i / 32] |= 1u << (i % 32);
}

static void tint_replaced(uint32_t *pending, uint32_t asset)
{
    pending[asset / 32] &= ~(1u << (asset % 32));
}

static int tint_pending_valid(struct cursor *c, const uint32_t *pending)
{
    for (uint32_t i = 0; i < CFW_THEME_MAX_ASSETS; i++) {
        struct layout b;
        if (bit(pending, i) && (!bitmap_layout(c, i, &b) || !b.mask))
            return 0;
    }
    return 1;
}

/* Pixel indices of a paletted record must name a palette entry. data holds the
 * record bytes, which start at the palette. */
static int indices_valid(const struct layout *b, const unsigned char *data)
{
    uint32_t colors = b->colors, width = b->width;
    /* Every 8-bit index names an entry of a full palette. */
    if (!colors || (b->depth == 8 && colors >= 256))
        return 1;
    const unsigned char *row = data + b->pixels - b->start;
    for (uint32_t y = 0; y < b->height; y++, row += b->stride) {
        if (b->depth == 8) {
            for (uint32_t x = 0; x < width; x++)
                if (row[x] >= colors)
                    return 0;
        } else {
            for (uint32_t x = 0; x < width; x++)
                if (u16(row + x * 2) >= colors)
                    return 0;
        }
    }
    return 1;
}

/* Reject malformed UTF-8 and hidden data after a terminating NUL. */
static int metadata_name(const unsigned char *text, unsigned size)
{
    unsigned at = 0;
    if (!size || !text[0])
        return 0;
    while (at < size && text[at]) {
        uint32_t cp = text[at++];
        unsigned tails = 0;
        uint32_t minimum = 0;
        if (cp >= 0xc2 && cp <= 0xdf) {
            cp &= 31;
            tails = 1;
            minimum = 0x80;
        } else if (cp >= 0xe0 && cp <= 0xef) {
            cp &= 15;
            tails = 2;
            minimum = 0x800;
        } else if (cp >= 0xf0 && cp <= 0xf4) {
            cp &= 7;
            tails = 3;
            minimum = 0x10000;
        } else if (cp >= 0x80)
            return 0;
        if (tails > size - at)
            return 0;
        while (tails--) {
            unsigned c = text[at++];
            if ((c & 0xc0) != 0x80)
                return 0;
            cp = (cp << 6) | (c & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
            cp < 32 || cp == 127)
            return 0;
    }
    if (at == size)
        return 0;
    for (; at < size; at++)
        if (text[at])
            return 0;
    return 1;
}

int cfw_theme_metadata_read(const struct osos_path *path,
                            struct cfw_theme_metadata *out)
{
    void *file;
    if (osos_file_open(path, 1, &file))
        return 0;
    struct cfw_theme_metadata candidate;
    int valid = exact(file, &candidate, sizeof(candidate)) && eof(file);
    osos_file_close(file);
    if (!valid || candidate.magic != RTM4_MAGIC || !fingerprint(candidate.catalog) ||
        !candidate.appearance_mask || (candidate.appearance_mask & ~3u) ||
        !candidate.id[0] || !cfw_theme_id_field_valid(candidate.id) ||
        !metadata_name((const unsigned char *)candidate.name, sizeof(candidate.name)))
        return 0;
    cfw_copy(out, &candidate, sizeof(candidate));
    return 1;
}

/* RTH4: 52-byte header, then the style prefix and bitmap records. */
static int header_valid(const struct pack_header *h, int dark)
{
    return h->magic == RTH4_MAGIC && fingerprint(h->catalog) &&
           h->appearance == (uint32_t)!!dark && h->count <= cfw_bitmap_asset_count &&
           cfw_bitmap_asset_count <= CFW_THEME_MAX_ASSETS &&
           h->size >= CFW_THEME_STYLE_BYTES &&
           h->size - CFW_THEME_STYLE_BYTES <= cfw_theme_max_bytes;
}

static int style_valid(const unsigned char *data)
{
    for (unsigned i = 0; i < CFW_THEME_COLOR_COUNT; i++)
        if (data[i * 4 + 3] != 255)
            return 0;
    const struct cfw_theme_options *o =
        (const void *)(data + sizeof(struct cfw_theme_style));
    if ((o->flags & ~(CFW_THEME_FLAT_SELECTION | CFW_THEME_MENU_COVERS_OFF |
                      CFW_THEME_FLAT_ARTWORK)) ||
        o->icon[3] != 255)
        return 0;
    for (unsigned a = 0; a < CFW_THEME_ACCENT_COUNT - 1; a++)
        for (unsigned slot = 0; slot < CFW_PALETTE_COUNT; slot++)
            if (o->accents[a][slot][3] != 255)
                return 0;
    for (unsigned i = 0; i < CFW_THEME_MAX_ASSETS; i++)
        if (o->tint[i] >= CFW_TINT_COUNT)
            return 0;
    return 1;
}

/* The header, the file size it declares and the style prefix are checked before
 * a caller saves the selection, so a stale, truncated or malformed pack leaves the
 * active theme unchanged. Seeking to the end reads nothing. */
static int open_pack(const struct osos_path *path, int dark,
                     struct cfw_theme_reader *reader)
{
    struct pack_header h;
    cfw_zero(reader, sizeof(*reader));
    if (osos_file_open(path, 1, &reader->file)) {
        reader->file = 0;
        return 0;
    }
    if (!exact(reader->file, &h, sizeof(h)) || !header_valid(&h, dark) ||
        osos_file_seek(reader->file, (int64_t)sizeof(h) + h.size, 0) ||
        !eof(reader->file) || osos_file_seek(reader->file, sizeof(h), 0)) {
        cfw_theme_reader_close(reader);
        return 0;
    }
    reader->size = h.size;
    reader->count = h.count;
    reader->crc = h.crc;
    return 1;
}

void cfw_theme_reader_close(struct cfw_theme_reader *reader)
{
    if (reader->file)
        osos_file_close(reader->file);
    reader->file = 0;
}

int cfw_theme_pack_open(const struct osos_path *path, int dark,
                        struct cfw_theme_reader *reader, unsigned char *style)
{
    if (!open_pack(path, dark, reader))
        return 0;
    if (exact(reader->file, style, CFW_THEME_STYLE_BYTES) && style_valid(style))
        return 1;
    cfw_theme_reader_close(reader);
    return 0;
}

int cfw_theme_pack_valid(const struct osos_path *path, int dark)
{
    struct cfw_theme_reader reader;
    uint32_t style[CFW_THEME_STYLE_BYTES / 4];
    int valid = cfw_theme_pack_open(path, dark, &reader, (unsigned char *)style);
    cfw_theme_reader_close(&reader);
    return valid;
}

/* The baseline lists every catalog asset once, so its body size is exact. */
static int open_baseline(const struct osos_path *path, struct cfw_theme_reader *reader)
{
    if (!open_pack(path, 0, reader))
        return 0;
    if (reader->count == cfw_bitmap_asset_count &&
        reader->size == CFW_THEME_STYLE_BYTES + cfw_theme_max_bytes)
        return 1;
    cfw_theme_reader_close(reader);
    return 0;
}

int cfw_theme_baseline_valid(const struct osos_path *path)
{
    struct cfw_theme_reader reader;
    int valid = open_baseline(path, &reader);
    cfw_theme_reader_close(&reader);
    return valid;
}

/* Frees a theme's heap copy when the live source changes. */
static void catalog_set(uint32_t position, const struct cfw_catalog_source *source)
{
    const unsigned char *live = cfw_catalog_sources[position].data;
    if (live != cfw_catalog_builtin[position].data)
        osos_import_free((void *)live);
    cfw_catalog_sources[position] = *source;
}

/* A replacement equal to the built-in art keeps the built-in source; any other
 * is run-packed into a heap copy after the previous copy is freed. */
static int catalog_install(uint32_t index, const unsigned char *pixels, uint32_t offset)
{
    uint32_t position = index - cfw_bitmap_firmware_count;
    catalog_set(position, &cfw_catalog_builtin[position]);
    if (~cfw_theme_crc32(cfw_theme_crc32(UINT32_MAX, cfw_catalog_header, offset),
                         pixels,
                         cfw_catalog_pixel_bytes) == cfw_bitmap_assets[index].crc)
        return 1;
    struct cfw_catalog_source source = {0, cfw_catalog_pack(pixels, 0)};
    unsigned char *data = osos_import_alloc(source.size);
    if (!data)
        return 0;
    cfw_catalog_pack(pixels, data);
    source.data = data;
    catalog_set(position, &source);
    return 1;
}

static void mark(uint32_t *bits, uint32_t index)
{
    bits[index / 32] |= 1u << (index % 32);
}

int cfw_theme_pack_apply(struct cfw_theme_reader *reader, const unsigned char *style,
                         uint32_t *assets)
{
    void *file = reader->file;
    uint32_t pending[CFW_THEME_BITMAP_WORDS];
    uint32_t crc = cfw_theme_crc32(UINT32_MAX, style, CFW_THEME_STYLE_BYTES);
    uint32_t consumed = CFW_THEME_STYLE_BYTES, previous = 0;
    struct cursor c;
    cursor_init(&c);
    tint_pending(style, pending);
    for (uint32_t i = 0; i < reader->count; i++) {
        struct record r;
        struct layout b;
        if (reader->size - consumed < sizeof(r) || !exact(file, &r, sizeof(r)))
            return 0;
        consumed += sizeof(r);
        crc = cfw_theme_crc32(crc, &r, sizeof(r));
        uint32_t padding = (0u - r.size) & 3u;
        if (!record_valid(&c, &r, i, previous, &b) ||
            r.size > reader->size - consumed ||
            padding > reader->size - consumed - r.size ||
            (packed(r.asset) && r.size != cfw_catalog_pixel_bytes))
            return 0;
        previous = r.asset;
        mark(assets, r.asset);
        unsigned char *data = packed(r.asset) ? cfw_catalog_slot + r.offset
                                              : asset_data(&c, r.asset) + r.offset;
        /* Resident bytes must never hold an index past the palette. */
        if (!exact(file, data, r.size) || !indices_valid(&b, data)) {
            cfw_zero(data, r.size);
            return 0;
        }
        crc = cfw_theme_crc32(crc, data, r.size);
        uint32_t zero = 0;
        if (padding && (!exact(file, &zero, padding) || zero))
            return 0;
        crc = cfw_theme_crc32(crc, &zero, padding);
        consumed += r.size + padding;
        if (packed(r.asset) && !catalog_install(r.asset, data, r.offset))
            return 0;
        tint_replaced(pending, r.asset);
    }
    return consumed == reader->size && ~crc == reader->crc && eof(file) &&
           tint_pending_valid(&c, pending);
}

int cfw_theme_baseline_restore(const struct osos_path *path, const uint32_t *assets)
{
    uint32_t last = 0;
    int firmware = 0;
    for (uint32_t i = 0; i < cfw_bitmap_asset_count; i++) {
        if (!bit(assets, i))
            continue;
        if (packed(i)) {
            uint32_t position = i - cfw_bitmap_firmware_count;
            catalog_set(position, &cfw_catalog_builtin[position]);
        } else {
            last = i;
            firmware = 1;
        }
    }
    if (!firmware)
        return 1;
    struct cfw_theme_reader reader;
    if (!open_baseline(path, &reader))
        return 0;
    struct cursor c;
    cursor_init(&c);
    int valid = 1;
    /* Records are complete and sorted, so record i holds asset i and starts after
     * the lengths of the records before it. Only the requested ones are read, up
     * to the last; each must match its original checksum. */
    uint32_t at = sizeof(struct pack_header) + CFW_THEME_STYLE_BYTES;
    for (uint32_t i = 0; valid && i <= last; i++) {
        struct layout b;
        valid = bitmap_layout(&c, i, &b);
        if (!valid)
            break;
        uint32_t length = cfw_bitmap_assets[i].size - b.start;
        uint32_t next = at + sizeof(struct record) + ((length + 3u) & ~3u);
        if (!bit(assets, i)) {
            at = next;
            continue;
        }
        struct record r;
        valid = !osos_file_seek(reader.file, at, 0) &&
                exact(reader.file, &r, sizeof(r)) && r.asset == i &&
                record_valid(&c, &r, i, i - 1, &b);
        if (!valid)
            break;
        unsigned char *bitmap = asset_data(&c, i), *data = bitmap + r.offset;
        if (!exact(reader.file, data, r.size) || !indices_valid(&b, data)) {
            cfw_zero(data, r.size);
            valid = 0;
            break;
        }
        valid = ~cfw_theme_crc32(cfw_theme_crc32(UINT32_MAX, bitmap, r.offset), data,
                                 r.size) == cfw_bitmap_assets[i].crc;
        at = next;
    }
    cfw_theme_reader_close(&reader);
    return valid;
}

int cfw_theme_asset_from_pixels(const unsigned char *pixels, uint32_t *index)
{
    struct cursor c;
    cursor_init(&c);
    if (!pixels)
        return 0;
    uint32_t position;
    if (cfw_catalog_slot_position(pixels, &position)) {
        *index = cfw_bitmap_firmware_count + position;
        return 1;
    }
    for (uint32_t i = 0; i < cfw_bitmap_firmware_count && i < CFW_THEME_MAX_ASSETS;
         i++) {
        const unsigned char *data = asset_data(&c, i);
        uint32_t size = cfw_bitmap_assets[i].size;
        if (pixels >= data && pixels < data + size) {
            struct layout b;
            if (pixels != data + CFW_BMAP_HEADER_BYTES || !bitmap_layout(&c, i, &b))
                return 0;
            *index = i;
            return 1;
        }
    }
    return 0;
}

unsigned char *cfw_theme_asset_palette(uint32_t index, uint32_t *colors)
{
    struct cursor c;
    struct layout b;
    cursor_init(&c);
    if (!bitmap_layout(&c, index, &b) || !b.colors)
        return 0;
    *colors = b.colors;
    return asset_data(&c, index) + b.start;
}

unsigned char *cfw_theme_asset_argb(uint32_t index, uint32_t *width, uint32_t *height,
                                    uint32_t *stride)
{
    struct cursor c;
    struct layout b;
    cursor_init(&c);
    if (!bitmap_layout(&c, index, &b) || b.depth != 32)
        return 0;
    *width = b.width;
    *height = b.height;
    *stride = b.stride;
    return asset_data(&c, index) + b.pixels;
}

int cfw_theme_asset_center_pixel(uint32_t index, unsigned char out[4])
{
    struct cursor c;
    struct layout b;
    cursor_init(&c);
    if (!bitmap_layout(&c, index, &b) || b.mask || b.depth == 16)
        return 0;
    const unsigned char *data = asset_data(&c, index);
    const unsigned char *row = data + b.pixels + ((b.height - 1) / 2) * b.stride;
    const unsigned char *pixel;
    if (b.colors) {
        uint32_t entry = row[(b.width - 1) / 2];
        if (entry >= b.colors)
            return 0;
        pixel = data + b.start + entry * 4;
    } else {
        pixel = row + ((b.width - 1) / 2) * 4;
    }
    if (!pixel[3])
        return 0;
    out[0] = pixel[2];
    out[1] = pixel[1];
    out[2] = pixel[0];
    out[3] = 255;
    return 1;
}
