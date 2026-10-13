/* SPDX-License-Identifier: GPL-3.0-only */

#include "assets.h"
#include "cfw.h"
#include "theme_render.h"
#include "osos.h"
#include "patch.h"

/* RTH2 adds accent/name metadata after the RTH1 header. The aligned XOR records
 * retain stock bitmap headers and restore the original bytes on disable. */
struct theme_header {
    uint32_t magic;
    unsigned char catalog[32];
    uint32_t count, size, crc;
};

struct theme_record {
    uint32_t asset, offset, size;
    unsigned char delta[];
};

static const struct osos_path theme_path = {0, "themes\\dark\\theme.bin"};
static unsigned char *theme;
static uint32_t record_count;
static int active;
static unsigned char *pending;
static uint32_t pending_count;
static struct cfw_theme_metadata current_metadata, pending_metadata;

void *cfw_bitmap_resource_get_original(void *service, uint32_t type, uint32_t id);

PATCH_ARM const void *cfw_bitmap_resource_get(void *service, uint32_t type, uint32_t id)
{
    const void *resource = cfw_bitmap_resource_get_original(service, type, id);
    return type == OSOS_RESOURCE_COLR ? cfw_theme_color(id, resource) : resource;
}

static uint32_t checksum(const unsigned char *data, uint32_t size)
{
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static unsigned char delta_byte(const struct theme_record *record, uint32_t offset)
{
    return record && offset >= record->offset && offset - record->offset < record->size
               ? record->delta[offset - record->offset]
               : 0;
}

static const struct theme_record *active_record(uint32_t index)
{
    uint32_t position = 0;
    for (uint32_t i = 0; active && i < record_count; i++) {
        const struct theme_record *record = (const void *)(theme + position);
        if (record->asset == index)
            return record;
        if (record->asset > index)
            break;
        position += (sizeof(*record) + record->size + 3u) & ~3u;
    }
    return 0;
}

static unsigned char replaced_byte(const struct cfw_bitmap_asset *asset,
                                   const struct theme_record *previous,
                                   const struct theme_record *record, uint32_t offset)
{
    return asset->data[offset] ^ delta_byte(previous, offset) ^
           delta_byte(record, offset);
}

static uint32_t original_checksum(const struct cfw_bitmap_asset *asset,
                                  const struct theme_record *previous)
{
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < asset->size; i++) {
        crc ^= asset->data[i] ^ delta_byte(previous, i);
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static int valid_records(uint32_t size, uint32_t count)
{
    uint32_t position = 0, previous = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (size - position < sizeof(struct theme_record))
            return 0;

        const struct theme_record *record = (const void *)(pending + position);
        position += sizeof(*record);
        if (record->asset >= cfw_bitmap_asset_count ||
            (i && record->asset <= previous) || !record->size ||
            record->size > size - position)
            return 0;

        previous = record->asset;
        const struct cfw_bitmap_asset *asset = &cfw_bitmap_assets[record->asset];
        const struct theme_record *old = active_record(record->asset);
        if (record->offset < asset->start || record->offset > asset->size ||
            record->size > asset->size - record->offset ||
            original_checksum(asset, old) != asset->crc)
            return 0;

        if (asset->colors && record->offset + record->size > asset->pixels) {
            for (uint32_t y = 0; y < asset->height; y++) {
                for (uint32_t x = 0; x < asset->width; x++) {
                    uint32_t at =
                        asset->pixels + y * asset->stride + x * (asset->depth / 8);
                    uint32_t index = replaced_byte(asset, old, record, at);
                    if (asset->depth == 16)
                        index |= (uint32_t)replaced_byte(asset, old, record, at + 1)
                                 << 8;
                    if (index >= asset->colors)
                        return 0;
                }
            }
        }

        position += record->size;
        uint32_t padding = (0u - position) & 3u;
        if (padding > size - position)
            return 0;
        position += padding;
    }
    return position == size;
}

void cfw_assets_cancel(void)
{
    if (pending) {
        ((void (*)(void *))OSOS_IMPORT_FREE)(pending);
        pending = 0;
    }
    pending_count = 0;
}

static int read_exact(void *handle, void *data, uint32_t size)
{
    uint32_t count = 0;
    return !osos_file_read(handle, data, size, &count) && count == size;
}

static int read_header(void *handle, struct theme_header *header,
                       struct cfw_theme_metadata *metadata)
{
    cfw_zero_bytes(metadata, sizeof(*metadata));
    if (!read_exact(handle, header, sizeof(*header)) ||
        (header->magic != 0x31485452 && header->magic != 0x32485452) ||
        !header->count || header->count > cfw_bitmap_asset_count || !header->size ||
        header->size > cfw_theme_max_bytes)
        return 0;

    for (unsigned i = 0; i < sizeof(header->catalog); i++)
        if (header->catalog[i] != cfw_theme_catalog_hash[i])
            return 0;

    if (header->magic == 0x32485452) {
        if (!read_exact(handle, metadata, sizeof(*metadata)) ||
            metadata->accent > 0xffffff || metadata->name[sizeof(metadata->name) - 1])
            return 0;

        for (unsigned i = 0; metadata->name[i]; i++)
            if ((unsigned char)metadata->name[i] < 32)
                return 0;
    }
    return 1;
}

int cfw_assets_metadata(const struct osos_path *path,
                        struct cfw_theme_metadata *metadata)
{
    void *handle;
    if (osos_file_open(path, 1, &handle))
        return 0;

    struct theme_header header;
    int valid = read_header(handle, &header, metadata);
    osos_file_close(handle);
    return valid;
}

int cfw_assets_prepare(void)
{
    return active || pending || cfw_assets_prepare_path(&theme_path);
}

int cfw_assets_prepare_path(const struct osos_path *path)
{
    cfw_assets_cancel();

    void *handle;
    if (osos_file_open(path, 1, &handle))
        return 0;

    struct theme_header header;
    int valid = read_header(handle, &header, &pending_metadata);
    if (valid) {
        pending = ((void *(*)(uint32_t))OSOS_IMPORT_ALLOC)(header.size);
        valid = pending && read_exact(handle, pending, header.size);
    }

    if (valid) {
        valid = checksum(pending, header.size) == header.crc &&
                valid_records(header.size, header.count);
    }
    osos_file_close(handle);

    if (!valid) {
        cfw_assets_cancel();
        return 0;
    }

    pending_count = header.count;
    return 1;
}

static void apply(void)
{
    uint32_t position = 0;
    for (uint32_t i = 0; i < record_count; i++) {
        const struct theme_record *record = (const void *)(theme + position);
        unsigned char *target = cfw_bitmap_assets[record->asset].data + record->offset;
        for (uint32_t j = 0; j < record->size; j++)
            target[j] ^= record->delta[j];
        position += (sizeof(*record) + record->size + 3u) & ~3u;
    }
}

void cfw_assets_set_enabled(int enabled)
{
    if (enabled && !pending)
        return;

    if (active) {
        apply();
        ((void (*)(void *))OSOS_IMPORT_FREE)(theme);
        theme = 0;
        record_count = 0;
        active = 0;
    }

    if (enabled) {
        theme = pending;
        record_count = pending_count;
        cfw_copy_bytes(&current_metadata, &pending_metadata, sizeof(current_metadata));
        pending = 0;
        pending_count = 0;
        apply();
        active = 1;
    } else {
        cfw_assets_cancel();
        cfw_zero_bytes(&current_metadata, sizeof(current_metadata));
    }
}

int cfw_assets_enabled(void)
{
    return active;
}

uint32_t cfw_assets_accent(void)
{
    return current_metadata.accent;
}
