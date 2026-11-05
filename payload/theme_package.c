/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_package.h"
#include "assets.h"
#include "osos.h"

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
    uint32_t start, pixels, colors, stride, width, height, depth;
};

/* Explicit byte loops: the freestanding ARM payload has no libc. */
static void bytes_zero(void *destination, uint32_t size)
{
    unsigned char *p = destination;
    while (size--)
        *p++ = 0;
}

static void bytes_copy(void *destination, const void *source, uint32_t size)
{
    unsigned char *p = destination;
    const unsigned char *q = source;
    while (size--)
        *p++ = *q++;
}

static uint32_t crc_update(uint32_t crc, const unsigned char *data, uint32_t size)
{
    while (size--) {
        crc ^= *data++;
        for (unsigned i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static uint32_t u16(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8;
}

static uint32_t u32(const unsigned char *p)
{
    return u16(p) | u16(p + 2) << 16;
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

static int bitmap_layout(uint32_t index, struct layout *b)
{
    if (index >= cfw_bitmap_asset_count || index >= CFW_THEME_MAX_ASSETS)
        return 0;
    const struct cfw_bitmap_asset *asset = &cfw_bitmap_assets[index];
    const unsigned char *p = asset->data;
    if (asset->size < 28)
        return 0;
    b->start = u32(p + 24) == asset->size - 28 ? 28 : 32;
    if (asset->size < b->start || u32(p + b->start - 4) != asset->size - b->start)
        return 0;
    uint32_t format = u16(p);
    b->depth = format == 4                         ? 4
               : format == 8 || format == 0x64     ? 8
               : format == 0x65 || format == 0x565 ? 16
               : format == 0x1888                  ? 32
                                                   : 0;
    b->stride = u16(p + 4);
    b->height = u32(p + 16);
    b->width = u32(p + 20);
    b->colors = 0;
    if (!b->depth || u16(p + 6) != b->depth || !b->width || !b->height || !b->stride ||
        b->width > b->stride * 8 / b->depth)
        return 0;
    if (format == 0x64 || format == 0x65) {
        if (asset->size - b->start < 4)
            return 0;
        b->colors = u32(p + b->start);
        b->start += 4;
        if (!b->colors || b->colors > (1u << b->depth) ||
            b->colors > (asset->size - b->start) / 4)
            return 0;
    }
    b->pixels = b->start + b->colors * 4;
    return b->height <= (asset->size - b->pixels) / b->stride &&
           b->pixels + b->height * b->stride == asset->size;
}

static uint32_t record_bytes(const struct record *r)
{
    return sizeof(*r) + ((r->size + 3u) & ~3u);
}

static int records_valid(const struct cfw_theme_pack *pack)
{
    uint32_t at = pack->records_offset, previous = 0;
    for (uint32_t i = 0; i < pack->count; i++) {
        if (at > pack->size || pack->size - at < sizeof(struct record))
            return 0;
        const struct record *r = (const void *)(pack->data + at);
        struct layout b;
        if ((i && r->asset <= previous) || !bitmap_layout(r->asset, &b) ||
            r->offset != b.start ||
            r->size != cfw_bitmap_assets[r->asset].size - b.start ||
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
    }
    return at == pack->size;
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

static int reserved_id(const char *id)
{
    static const char *const reserved[] = {"default", "dark", "classic"};
    for (unsigned i = 0; i < sizeof(reserved) / sizeof(*reserved); i++) {
        unsigned j = 0;
        while (id[j] && id[j] == reserved[i][j])
            j++;
        if (!id[j] && !reserved[i][j])
            return 1;
    }
    return 0;
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
        !candidate.appearance_mask || (candidate.appearance_mask & ~3u))
        return 0;
    unsigned i = 0;
    for (; i < sizeof(candidate.id) && candidate.id[i]; i++) {
        unsigned char c = candidate.id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              (i && (c == '_' || c == '-'))))
            return 0;
    }
    if (!i || i == sizeof(candidate.id) || !candidate.name[0])
        return 0;
    for (; i < sizeof(candidate.id); i++)
        if (candidate.id[i])
            return 0;
    if (reserved_id(candidate.id) ||
        !metadata_name((const unsigned char *)candidate.name, sizeof(candidate.name)))
        return 0;
    bytes_copy(out, &candidate, sizeof(candidate));
    return 1;
}

void cfw_theme_pack_release(struct cfw_theme_pack *pack)
{
    if (pack->data)
        osos_import_free(pack->data);
    bytes_zero(pack, sizeof(*pack));
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
    const struct cfw_theme_options *o = (const void *)(data + sizeof(struct cfw_theme_style));
    if ((o->flags & ~(CFW_THEME_FLAT_SELECTION | CFW_THEME_TINT_PREVIEWS)) ||
        o->icon[3] != 255)
        return 0;
    for (unsigned a = 0; a < CFW_THEME_ACCENT_COUNT - 1; a++)
        for (unsigned slot = 0; slot < CFW_PALETTE_COUNT; slot++)
            if (o->accents[a][slot][3] != 255)
                return 0;
    return 1;
}

int cfw_theme_pack_read(const struct osos_path *path, int dark, int classic,
                        struct cfw_theme_pack *out)
{
    bytes_zero(out, sizeof(*out));
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
    valid = valid && ~crc_update(UINT32_MAX, out->data, out->size) == h.crc &&
            records_valid(out) && style_valid(out->data);
    if (!valid)
        cfw_theme_pack_release(out);
    return valid;
}

static int bit(const uint32_t *bits, unsigned index)
{
    return !!(bits[index / 32] & (1u << (index % 32)));
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

/* Stream the restore file. Only old-only records enter the transaction buffer;
 * the entire baseline is never held in RAM. CRC covers skipped bytes as well. */
static int restore_missing(const struct osos_path *path, struct cfw_theme_pack *stage,
                           const uint32_t *incoming, uint32_t needed, int complete)
{
    if (!needed && !complete)
        return 1;
    void *file;
    if (osos_file_open(path, 1, &file))
        return 0;

    struct pack_header h;
    int valid = exact(file, &h, sizeof(h)) && header_valid(&h, 0) &&
                (!complete || (h.count == cfw_bitmap_asset_count && h.count));
    uint32_t consumed = 0, crc = UINT32_MAX, previous = 0, stage_at = 0;
    unsigned char scratch[256];
    /* The baseline's style prefix is checksummed but not used. */
    while (valid && consumed < CFW_THEME_STYLE_BYTES) {
        uint32_t n = CFW_THEME_STYLE_BYTES - consumed;
        if (n > sizeof(scratch))
            n = sizeof(scratch);
        valid = exact(file, scratch, n);
        crc = crc_update(crc, scratch, n);
        consumed += n;
    }
    for (uint32_t i = 0; valid && i < h.count; i++) {
        struct record r;
        struct layout b;
        valid = h.size - consumed >= sizeof(r) && exact(file, &r, sizeof(r));
        if (!valid)
            break;
        consumed += sizeof(r);
        crc = crc_update(crc, (const void *)&r, sizeof(r));
        valid = (!i || r.asset > previous) && bitmap_layout(r.asset, &b) &&
                r.offset == b.start &&
                r.size == cfw_bitmap_assets[r.asset].size - b.start &&
                r.size <= h.size - consumed &&
                ((r.size + 3u) & ~3u) <= h.size - consumed;
        if (!valid)
            break;
        previous = r.asset;
        struct record *destination = !stage || bit(incoming, r.asset)
                                         ? 0
                                         : next_record(stage, &stage_at, r.asset);
        uint32_t original_crc =
            crc_update(UINT32_MAX, cfw_bitmap_assets[r.asset].data, r.offset);
        for (uint32_t at = 0; valid && at < r.size;) {
            uint32_t n = r.size - at;
            if (n > sizeof(scratch))
                n = sizeof(scratch);
            unsigned char *buffer = destination ? destination->data + at : scratch;
            valid = exact(file, buffer, n);
            if (!valid)
                break;
            crc = crc_update(crc, buffer, n);
            original_crc = crc_update(original_crc, buffer, n);
            at += n;
        }
        valid = valid && ~original_crc == cfw_bitmap_assets[r.asset].crc;
        uint32_t padding = (0u - r.size) & 3u;
        if (valid && padding) {
            valid = exact(file, scratch, padding);
            if (valid)
                crc = crc_update(crc, scratch, padding);
            for (uint32_t j = 0; valid && j < padding; j++)
                valid = !scratch[j];
        }
        consumed += r.size + padding;
        if (valid && destination)
            needed--;
    }
    valid = valid && !needed && consumed == h.size && ~crc == h.crc && eof(file);
    osos_file_close(file);
    return valid;
}

int cfw_theme_baseline_valid(const struct osos_path *path)
{
    return restore_missing(path, 0, 0, 0, 1);
}

int cfw_theme_transaction_bytes(const struct cfw_theme_pack *target,
                                const uint32_t *previous, uint32_t *bytes)
{
    *bytes = 0;
    if (cfw_bitmap_asset_count > CFW_THEME_MAX_ASSETS)
        return 0;
    uint32_t at = target ? target->records_offset : 0;
    for (uint32_t i = 0; i < cfw_bitmap_asset_count; i++) {
        if (next_record(target, &at, i) || !bit(previous, i))
            continue;
        struct layout b;
        if (!bitmap_layout(i, &b))
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

int cfw_theme_transaction_prepare(struct cfw_theme_pack *target, int dark,
                                  const uint32_t *previous,
                                  const struct osos_path *baseline,
                                  struct cfw_theme_transaction *out)
{
    bytes_zero(out, sizeof(*out));
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
        if (!r && bit(previous, i))
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
    for (uint32_t i = 0; i < cfw_bitmap_asset_count; i++) {
        const struct record *source = next_record(target, &target_at, i);
        if (source || !bit(previous, i))
            continue;
        struct layout b;
        if (!bitmap_layout(i, &b)) {
            cfw_theme_transaction_release(out);
            return 0;
        }
        struct record *r = (void *)(out->patch.data + at);
        r->asset = i;
        r->offset = b.start;
        r->size = cfw_bitmap_assets[i].size - b.start;
        uint32_t padded = (r->size + 3u) & ~3u;
        for (uint32_t j = 0; j < padded; j++)
            r->data[j] = 0;
        at += record_bytes(r);
    }
    if (!restore_missing(baseline, &out->patch, out->target_assets, missing, 0)) {
        cfw_theme_transaction_release(out);
        return 0;
    }
    if (target) {
        bytes_copy(&out->incoming, target, sizeof(*target));
        bytes_zero(target, sizeof(*target));
    }
    return 1;
}

static void pack_swap(struct cfw_theme_pack *pack)
{
    uint32_t at = pack->records_offset;
    for (uint32_t i = 0; i < pack->count; i++) {
        struct record *r = (void *)(pack->data + at);
        unsigned char *target = cfw_bitmap_assets[r->asset].data + r->offset;
        for (uint32_t j = 0; j < r->size; j++) {
            unsigned char old = target[j];
            target[j] = r->data[j];
            r->data[j] = old;
        }
        at += record_bytes(r);
    }
}

void cfw_theme_transaction_swap(struct cfw_theme_transaction *transaction)
{
    pack_swap(&transaction->incoming);
    pack_swap(&transaction->patch);
    transaction->applied = !transaction->applied;
}

void cfw_theme_transaction_release(struct cfw_theme_transaction *transaction)
{
    cfw_theme_pack_release(&transaction->incoming);
    cfw_theme_pack_release(&transaction->patch);
    bytes_zero(transaction, sizeof(*transaction));
}
