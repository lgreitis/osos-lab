/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_package.h"
#include "assets.h"
#include "catalog_bitmaps.h"
#include "cfw.h"
#include "osos.h"
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
 * records exchange sources (catalog_bitmaps.h) instead of bytes. */
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

uint32_t cfw_theme_crc32(uint32_t crc, const void *data, uint32_t size)
{
    const unsigned char *p = data;
    while (size--) {
        crc ^= *p++;
        for (unsigned i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
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

static uint32_t record_bytes(const struct record *r)
{
    return sizeof(*r) + ((r->size + 3u) & ~3u);
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

/* One bit per tinted asset; records_valid clears each bit as its record arrives
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

static int records_valid(const struct cfw_theme_pack *pack)
{
    uint32_t at = pack->records_offset, previous = 0;
    uint32_t pending[CFW_THEME_BITMAP_WORDS];
    struct cursor c;
    cursor_init(&c);
    tint_pending(pack->data, pending);
    for (uint32_t i = 0; i < pack->count; i++) {
        if (at > pack->size || pack->size - at < sizeof(struct record))
            return 0;
        const struct record *r = (const void *)(pack->data + at);
        struct layout b;
        if (!record_valid(&c, r, i, previous, &b) ||
            r->size > pack->size - at - sizeof(*r))
            return 0;
        uint32_t length = record_bytes(r);
        if (length > pack->size - at)
            return 0;
        for (uint32_t j = sizeof(*r) + r->size; j < length; j++)
            if (pack->data[at + j])
                return 0;
        if (b.colors) {
            for (uint32_t y = 0; y < b.height; y++)
                for (uint32_t x = 0; x < b.width; x++) {
                    const unsigned char *p =
                        r->data + b.pixels - b.start + y * b.stride + x * (b.depth / 8);
                    if ((b.depth == 8 ? p[0] : u16(p)) >= b.colors)
                        return 0;
                }
        }
        previous = r->asset;
        at += length;
        tint_replaced(pending, r->asset);
    }
    return at == pack->size && tint_pending_valid(&c, pending);
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

void cfw_theme_pack_release(struct cfw_theme_pack *pack)
{
    if (pack->data)
        osos_import_free(pack->data);
    cfw_zero(pack, sizeof(*pack));
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

int cfw_theme_pack_read(const struct osos_path *path, int dark, int classic,
                        struct cfw_theme_pack *out)
{
    cfw_zero(out, sizeof(*out));
    void *file;
    if (osos_file_open(path, 1, &file))
        return 0;
    struct pack_header h;
    int valid = exact(file, &h, sizeof(h)) && header_valid(&h, dark);
    if (valid) {
        out->data = osos_import_alloc(h.size);
        out->count = h.count;
        out->size = h.size;
        out->records_offset = CFW_THEME_STYLE_BYTES;
        out->custom = !classic;
        valid = out->data && exact(file, out->data, out->size) && eof(file);
    }
    osos_file_close(file);
    valid = valid && ~cfw_theme_crc32(UINT32_MAX, out->data, out->size) == h.crc &&
            records_valid(out) && style_valid(out->data);
    if (!valid)
        cfw_theme_pack_release(out);
    return valid;
}

/* Walk validated, sorted records once while visiting ascending asset indices. */
static struct record *next_record(const struct cfw_theme_pack *pack, uint32_t *at,
                                  uint32_t index)
{
    if (!pack || !pack->data || *at >= pack->size)
        return 0;
    struct record *r = (void *)(pack->data + *at);
    if (r->asset != index)
        return 0;
    *at += record_bytes(r);
    return r;
}

enum { SCRATCH_BYTES = CFW_THEME_STYLE_BYTES > 256 ? CFW_THEME_STYLE_BYTES : 256 };

/* Stream a pack through a small scratch buffer; the file is never held in RAM.
 * Baseline mode (complete or stage) requires every record to match the original
 * artwork checksum; with a stage, records for assets that the incoming pack omits
 * are copied into the stage's prepared records. Otherwise this probes an
 * appearance pack, whose style must be opaque. The CRC covers skipped bytes. */
static int stream_pack(const struct osos_path *path, int dark, int complete,
                       struct cfw_theme_pack *stage, const uint32_t *incoming,
                       uint32_t needed)
{
    if (stage && !needed)
        return 1;
    int baseline = complete || stage;
    void *file;
    if (osos_file_open(path, 1, &file))
        return 0;

    struct pack_header h;
    unsigned char scratch[SCRATCH_BYTES];
    uint32_t pending[CFW_THEME_BITMAP_WORDS];
    int valid = exact(file, &h, sizeof(h)) && header_valid(&h, dark) &&
                (!complete || (h.count == cfw_bitmap_asset_count && h.count)) &&
                exact(file, scratch, CFW_THEME_STYLE_BYTES) &&
                (baseline || style_valid(scratch));
    uint32_t crc =
        valid ? cfw_theme_crc32(UINT32_MAX, scratch, CFW_THEME_STYLE_BYTES) : 0;
    uint32_t consumed = CFW_THEME_STYLE_BYTES, previous = 0, stage_at = 0;
    struct cursor c;
    cursor_init(&c);
    if (valid)
        tint_pending(scratch, pending);
    for (uint32_t i = 0; valid && i < h.count; i++) {
        struct record r;
        struct layout b;
        valid = h.size - consumed >= sizeof(r) && exact(file, &r, sizeof(r));
        if (!valid)
            break;
        consumed += sizeof(r);
        crc = cfw_theme_crc32(crc, &r, sizeof(r));
        valid = record_valid(&c, &r, i, previous, &b) && r.size <= h.size - consumed &&
                ((r.size + 3u) & ~3u) <= h.size - consumed;
        if (!valid)
            break;
        previous = r.asset;
        struct record *destination = !stage || bit(incoming, r.asset)
                                         ? 0
                                         : next_record(stage, &stage_at, r.asset);
        uint32_t original_crc =
            cfw_theme_crc32(UINT32_MAX, asset_data(&c, r.asset), r.offset);
        for (uint32_t at = 0; valid && at < r.size;) {
            uint32_t n = r.size - at;
            if (n > sizeof(scratch))
                n = sizeof(scratch);
            unsigned char *buffer = destination ? destination->data + at : scratch;
            valid = exact(file, buffer, n);
            if (!valid)
                break;
            crc = cfw_theme_crc32(crc, buffer, n);
            original_crc = cfw_theme_crc32(original_crc, buffer, n);
            at += n;
        }
        valid = valid && (!baseline || ~original_crc == cfw_bitmap_assets[r.asset].crc);
        uint32_t padding = (0u - r.size) & 3u;
        if (valid && padding) {
            valid = exact(file, scratch, padding);
            if (valid)
                crc = cfw_theme_crc32(crc, scratch, padding);
            for (uint32_t j = 0; valid && j < padding; j++)
                valid = !scratch[j];
        }
        consumed += r.size + padding;
        if (valid && destination)
            needed--;
        tint_replaced(pending, r.asset);
    }
    valid = valid && !needed && consumed == h.size && ~crc == h.crc && eof(file) &&
            (baseline || tint_pending_valid(&c, pending));
    osos_file_close(file);
    return valid;
}

int cfw_theme_baseline_valid(const struct osos_path *path)
{
    return stream_pack(path, 0, 1, 0, 0, 0);
}

int cfw_theme_pack_valid(const struct osos_path *path, int dark)
{
    return stream_pack(path, dark, 0, 0, 0, 0);
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

int cfw_theme_transaction_bytes(const struct cfw_theme_pack *target,
                                const uint32_t *previous, uint32_t *bytes)
{
    *bytes = 0;
    if (cfw_bitmap_asset_count > CFW_THEME_MAX_ASSETS)
        return 0;
    uint32_t at = target ? target->records_offset : 0;
    struct cursor c;
    cursor_init(&c);
    for (uint32_t i = 0; i < cfw_bitmap_asset_count; i++) {
        if (next_record(target, &at, i) || !bit(previous, i) || packed(i))
            continue;
        struct layout b;
        if (!bitmap_layout(&c, i, &b))
            return 0;
        uint32_t size = cfw_bitmap_assets[i].size - b.start;
        if (size > UINT32_MAX - 3u - sizeof(struct record))
            return 0;
        uint32_t length = sizeof(struct record) + ((size + 3u) & ~3u);
        if (*bytes > cfw_theme_max_bytes || length > cfw_theme_max_bytes - *bytes)
            return 0;
        *bytes += length;
    }
    return 1;
}

/* Catalog bitmap sources for the swap: a heap copy of each replacement in target
 * that differs from the built-in art, and the built-in art for every other
 * replacement and for each previous replacement that target drops. */
static int prepare_sources(const struct cfw_theme_pack *target,
                           const uint32_t *previous, struct cfw_theme_transaction *out)
{
    uint32_t at = target ? target->records_offset : 0;
    for (uint32_t i = 0; i < cfw_bitmap_asset_count; i++) {
        const struct record *r = next_record(target, &at, i);
        if (!packed(i) || (!r && !bit(previous, i)))
            continue;
        uint32_t position = i - cfw_bitmap_firmware_count;
        struct cfw_catalog_source *source = &out->sources[position];
        *source = cfw_catalog_builtin[position];
        out->source_mask |= 1u << position;
        if (!r ||
            ~cfw_theme_crc32(cfw_theme_crc32(UINT32_MAX, cfw_catalog_header, r->offset),
                             r->data, r->size) == cfw_bitmap_assets[i].crc)
            continue;
        uint32_t size = cfw_catalog_pack(r->data, 0);
        unsigned char *data = osos_import_alloc(size);
        if (!data)
            return 0;
        cfw_catalog_pack(r->data, data);
        source->data = data;
        source->size = size;
    }
    return 1;
}

int cfw_theme_transaction_prepare(struct cfw_theme_pack *target, int dark,
                                  const uint32_t *previous,
                                  const struct osos_path *baseline,
                                  struct cfw_theme_transaction *out)
{
    cfw_zero(out, sizeof(*out));
    if (cfw_bitmap_asset_count > CFW_THEME_MAX_ASSETS)
        return 0;
    out->dark = !!dark;
    out->custom = target && target->custom;
    uint32_t total, missing = 0;
    if (!cfw_theme_transaction_bytes(target, previous, &total))
        return 0;
    uint32_t target_at = target ? target->records_offset : 0;
    for (uint32_t i = 0; i < cfw_bitmap_asset_count; i++) {
        const struct record *r = next_record(target, &target_at, i);
        if (r)
            out->target_assets[i / 32] |= 1u << (i % 32);
        if (!r && bit(previous, i) && !packed(i))
            missing++;
    }
    if (total) {
        out->patch.data = osos_import_alloc(total);
        if (!out->patch.data)
            return 0;
    }
    out->patch.size = total;
    out->patch.count = missing;
    uint32_t at = 0;
    target_at = target ? target->records_offset : 0;
    struct cursor c;
    cursor_init(&c);
    for (uint32_t i = 0; i < cfw_bitmap_asset_count; i++) {
        const struct record *source = next_record(target, &target_at, i);
        if (source || !bit(previous, i) || packed(i))
            continue;
        struct layout b;
        if (!bitmap_layout(&c, i, &b)) {
            cfw_theme_transaction_release(out);
            return 0;
        }
        struct record *r = (void *)(out->patch.data + at);
        r->asset = i;
        r->offset = b.start;
        r->size = cfw_bitmap_assets[i].size - b.start;
        cfw_zero(r->data, (r->size + 3u) & ~3u);
        at += record_bytes(r);
    }
    if (!stream_pack(baseline, 0, 0, &out->patch, out->target_assets, missing) ||
        !prepare_sources(target, previous, out)) {
        cfw_theme_transaction_release(out);
        return 0;
    }
    if (target) {
        cfw_copy(&out->incoming, target, sizeof(*target));
        cfw_zero(target, sizeof(*target));
    }
    return 1;
}

static void pack_swap(struct cfw_theme_pack *pack)
{
    uint32_t at = pack->records_offset;
    struct cursor c;
    cursor_init(&c);
    for (uint32_t i = 0; i < pack->count; i++) {
        struct record *r = (void *)(pack->data + at);
        at += record_bytes(r);
        if (packed(r->asset))
            continue;
        unsigned char *target = asset_data(&c, r->asset) + r->offset;
        for (uint32_t j = 0; j < r->size; j++) {
            unsigned char old = target[j];
            target[j] = r->data[j];
            r->data[j] = old;
        }
    }
}

void cfw_theme_transaction_swap(struct cfw_theme_transaction *transaction)
{
    pack_swap(&transaction->incoming);
    pack_swap(&transaction->patch);
    for (uint32_t p = 0; p < cfw_catalog_bitmap_count; p++) {
        if (!(transaction->source_mask & (1u << p)))
            continue;
        struct cfw_catalog_source live = cfw_catalog_sources[p];
        cfw_catalog_sources[p] = transaction->sources[p];
        transaction->sources[p] = live;
    }
    cfw_catalog_reload();
    transaction->applied = !transaction->applied;
}

void cfw_theme_transaction_release(struct cfw_theme_transaction *transaction)
{
    for (uint32_t p = 0; p < cfw_catalog_bitmap_count; p++)
        if ((transaction->source_mask & (1u << p)) &&
            transaction->sources[p].data != cfw_catalog_builtin[p].data)
            osos_import_free((void *)transaction->sources[p].data);
    cfw_theme_pack_release(&transaction->incoming);
    cfw_theme_pack_release(&transaction->patch);
    cfw_zero(transaction, sizeof(*transaction));
}
