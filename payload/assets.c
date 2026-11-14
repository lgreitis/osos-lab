/* SPDX-License-Identifier: GPL-3.0-only */

#include "assets.h"
#include "osos.h"

/* RTH3 records replace palette/pixel storage without changing native headers. */
struct theme_header {
    uint32_t magic;
    unsigned char catalog[32];
    uint32_t count, size, crc;
};

struct theme_record {
    uint32_t asset, offset, size;
    unsigned char data[];
};

static const struct osos_path theme_paths[] = {
    {0, "themes\\default\\theme.bin"},
    {0, "themes\\dark\\theme.bin"},
};
static unsigned char *theme;
static uint32_t record_count, record_layout;
static int active;

struct bitmap_layout {
    uint32_t start, pixels, colors, stride, width, height, depth;
};

static uint32_t read16(const unsigned char *data)
{
    return data[0] | (uint32_t)data[1] << 8;
}

static uint32_t read32(const unsigned char *data)
{
    return read16(data) | read16(data + 2) << 16;
}

/* Theme records preserve the native header and palette count. */
static int bitmap_layout(const struct cfw_bitmap_asset *asset, struct bitmap_layout *b)
{
    const unsigned char *data = asset->data;
    if (asset->size < 28)
        return 0;
    b->start = read32(data + 24) == asset->size - 28 ? 28 : 32;
    if (asset->size < b->start || read32(data + b->start - 4) != asset->size - b->start)
        return 0;
    uint32_t format = read16(data);
    b->depth = read16(data + 6);
    uint32_t depth = format == 4                         ? 4
                     : format == 8 || format == 0x64     ? 8
                     : format == 0x65 || format == 0x565 ? 16
                     : format == 0x1888                  ? 32
                                                         : 0;
    b->stride = read16(data + 4);
    b->height = read32(data + 16);
    b->width = read32(data + 20);
    if (!depth || b->depth != depth || !b->width || !b->height || !b->stride ||
        b->width > b->stride * 8 / depth)
        return 0;
    b->colors = 0;
    b->pixels = b->start;
    if (format == 0x64 || format == 0x65) {
        if (asset->size - b->start < 4)
            return 0;
        b->colors = read32(data + b->start);
        b->start += 4;
        if (!b->colors || b->colors > (1u << depth) ||
            b->colors > (asset->size - b->start) / 4)
            return 0;
        b->pixels = b->start + b->colors * 4;
    }
    return b->height <= (asset->size - b->pixels) / b->stride &&
           b->pixels + b->stride * b->height == asset->size;
}

static uint32_t checksum(uint32_t crc, const unsigned char *data, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static int valid_records(uint32_t size, uint32_t count)
{
    uint32_t position = 0, previous = 0, layout = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++) {
        if (size - position < sizeof(struct theme_record))
            return 0;

        const struct theme_record *record = (const void *)(theme + position);
        position += sizeof(*record);
        if (record->asset >= cfw_bitmap_asset_count ||
            (i && record->asset <= previous) || !record->size ||
            record->size > size - position)
            return 0;

        previous = record->asset;
        const struct cfw_bitmap_asset *asset = &cfw_bitmap_assets[record->asset];
        struct bitmap_layout bitmap;
        if (!bitmap_layout(asset, &bitmap) || record->offset != bitmap.start ||
            record->size != asset->size - bitmap.start)
            return 0;
        uint32_t crc = checksum(UINT32_MAX, asset->data, bitmap.start);
        crc = checksum(crc, active ? record->data : asset->data + bitmap.start,
                       record->size);
        if (~crc != asset->crc)
            return 0;
        if (bitmap.colors) {
            for (uint32_t y = 0; y < bitmap.height; y++) {
                for (uint32_t x = 0; x < bitmap.width; x++) {
                    uint32_t at = bitmap.pixels - bitmap.start + y * bitmap.stride +
                                  x * (bitmap.depth / 8);
                    uint32_t index = record->data[at];
                    if (bitmap.depth == 16)
                        index |= (uint32_t)record->data[at + 1] << 8;
                    if (index >= bitmap.colors)
                        return 0;
                }
            }
        }
        layout = checksum(layout, (const void *)record, sizeof(*record));

        position += record->size;
        uint32_t padding = (0u - position) & 3u;
        if (padding > size - position)
            return 0;
        position += padding;
    }
    if (position != size ||
        (active && (count != record_count || layout != record_layout)))
        return 0;
    record_layout = layout;
    return 1;
}

void cfw_assets_cancel(void)
{
    if (theme) {
        osos_import_free(theme);
        theme = 0;
    }
}

static int read_exact(void *handle, void *data, uint32_t size)
{
    uint32_t count = 0;
    return !osos_file_read(handle, data, size, &count) && count == size;
}

int cfw_assets_prepare(void)
{
    if (theme)
        return 1;
    void *handle;
    if (osos_file_open(&theme_paths[!active], 1, &handle))
        return 0;

    struct theme_header header;
    int valid = read_exact(handle, &header, sizeof(header)) &&
                header.magic == 0x33485452 && header.count &&
                header.count <= cfw_bitmap_asset_count && header.size &&
                header.size <= cfw_theme_max_bytes;
    for (unsigned i = 0; valid && i < sizeof(header.catalog); i++)
        valid = header.catalog[i] == cfw_theme_catalog_hash[i];
    if (valid) {
        theme = osos_import_alloc(header.size);
        valid = theme && read_exact(handle, theme, header.size);
    }

    if (valid) {
        valid = ~checksum(UINT32_MAX, theme, header.size) == header.crc &&
                valid_records(header.size, header.count);
    }
    osos_file_close(handle);

    if (!valid) {
        cfw_assets_cancel();
        return 0;
    }

    record_count = header.count;
    return 1;
}

static void apply(void)
{
    uint32_t position = 0;
    for (uint32_t i = 0; i < record_count; i++) {
        const struct theme_record *record = (const void *)(theme + position);
        unsigned char *target = cfw_bitmap_assets[record->asset].data + record->offset;
        for (uint32_t j = 0; j < record->size; j++)
            target[j] = record->data[j];
        position += (sizeof(*record) + record->size + 3u) & ~3u;
    }
}

void cfw_assets_set_enabled(int enabled)
{
    enabled = enabled != 0;
    if (enabled != active && theme) {
        apply();
        active = enabled;
    }
    cfw_assets_cancel();
}

int cfw_assets_enabled(void)
{
    return active;
}
