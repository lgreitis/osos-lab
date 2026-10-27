/* SPDX-License-Identifier: GPL-3.0-only */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM
#include "../../../payload/assets.h"

static unsigned char stock[] = {100, 0, 0, 0, 2, 0, 8, 0,   0, 0, 0,  0,   0, 0,
                                0,   0, 1, 0, 0, 0, 2, 0,   0, 0, 14, 0,   0, 0,
                                2,   0, 0, 0, 3, 2, 1, 255, 6, 5, 4,  255, 0, 1};
static unsigned char original[sizeof(stock)];
const struct cfw_bitmap_asset cfw_bitmap_assets[] = {
    {stock, sizeof(stock), 0x25789516}};
const uint32_t cfw_bitmap_asset_count = 1, cfw_theme_max_bytes = 24;
const unsigned char cfw_theme_catalog_hash[32] = {1, 2, 3};

static union {
    uint32_t alignment;
    unsigned char data[128];
} disk;

static uint32_t disk_size, cursor;
static int missing, short_read, no_memory, allocations;

struct osos_path {
    uint16_t volume;
    char text[258];
};

#define OSOS_RESOURCE_COLR 0x434f4c52

static void *allocate(uint32_t size)
{
    if (no_memory)
        return NULL;
    allocations++;
    return malloc(size);
}

static void release(void *data)
{
    allocations--;
    free(data);
}

static int osos_file_open(const struct osos_path *path, uint32_t mode, void **handle)
{
    assert(path->volume == 0 &&
           !strcmp(path->text, cfw_assets_enabled() ? "themes\\default\\theme.bin"
                                                    : "themes\\dark\\theme.bin") &&
           mode == 1);
    if (missing)
        return -1;
    cursor = 0;
    *handle = &disk;
    return 0;
}

static int osos_file_read(void *handle, void *data, uint32_t size, uint32_t *count)
{
    assert(handle == &disk);
    *count = size < disk_size - cursor ? size : disk_size - cursor;
    if (short_read && *count)
        --*count;
    memcpy(data, disk.data + cursor, *count);
    cursor += *count;
    return 0;
}

static void osos_file_close(void *handle)
{
    assert(handle == &disk);
}

#define OSOS_IMPORT_ALLOC allocate
#define OSOS_IMPORT_FREE release

void *cfw_bitmap_resource_get_original(void *service, uint32_t type, uint32_t id)
{
    (void)service;
    (void)type;
    (void)id;
    return stock;
}

const void *cfw_dark_mode_color(uint32_t id, const void *color)
{
    assert(id == 7 && color == stock);
    return stock + 1;
}

#include "../../../payload/assets.c"

static void pack(int dark)
{
    memset(disk.data, 0, sizeof(disk.data));
    struct theme_header *header = (void *)disk.data;
    header->magic = 0x33485452;
    memcpy(header->catalog, cfw_theme_catalog_hash, 32);
    header->count = 1;
    header->size = 24;
    struct theme_record *record = (void *)(disk.data + sizeof(*header));
    record->offset = 32;
    record->size = sizeof(stock) - 32;
    memcpy(record->data, original + 32, record->size);
    if (dark)
        record->data[0] ^= 0xaa;
    header->crc = ~checksum(UINT32_MAX, (const void *)record, header->size);
    disk_size = sizeof(*header) + header->size;
}

static void rejected(const unsigned char *original)
{
    int was_active = active;
    assert(!cfw_assets_prepare());
    assert(!theme && !allocations && active == was_active);
    assert(!memcmp(stock, original, sizeof(stock)));
}

static void check_layouts(void)
{
    const uint32_t formats[] = {4, 8, 0x64, 0x65, 0x565, 0x1888};
    const uint32_t depths[] = {4, 8, 8, 16, 16, 32};
    for (unsigned header = 28; header <= 32; header += 4) {
        for (unsigned i = 0; i < 6; i++) {
            unsigned char data[256] = {0};
            uint32_t colors = i == 2 || i == 3 ? 2 : 0;
            uint32_t pixels = header + (colors ? 4 + colors * 4 : 0);
            uint32_t size = pixels + 32;
            data[0] = formats[i];
            data[1] = formats[i] >> 8;
            data[4] = 16;
            data[6] = depths[i];
            data[16] = 2;
            data[20] = 4;
            data[header - 4] = size - header;
            if (colors)
                data[header] = colors;
            struct cfw_bitmap_asset asset = {data, size, 0};
            struct bitmap_layout layout;
            assert(bitmap_layout(&asset, &layout));
            assert(layout.start == header + (colors ? 4 : 0));
            assert(layout.pixels == pixels && layout.colors == colors);
            assert(layout.width == 4 && layout.height == 2 && layout.stride == 16);
            assert(layout.depth == depths[i]);
            for (unsigned truncated = 0; truncated < size; truncated++) {
                asset.size = truncated;
                assert(!bitmap_layout(&asset, &layout));
            }
            asset.size = size;
            data[4] = 0;
            assert(!bitmap_layout(&asset, &layout));
            data[4] = 16;
            memset(data + 16, 255, 4);
            assert(!bitmap_layout(&asset, &layout));
        }
    }
}

int main(void)
{
    check_layouts();
    memcpy(original, stock, sizeof(stock));
    assert(~checksum(UINT32_MAX, stock, sizeof(stock)) == cfw_bitmap_assets[0].crc);
    pack(1);
    assert(cfw_assets_prepare() && allocations == 1);
    assert(!memcmp(stock, original, sizeof(stock)));
    cfw_assets_set_enabled(1);
    assert(stock[32] == (original[32] ^ 0xaa));
    assert(!memcmp(stock + 40, original + 40, 2));
    cfw_assets_set_enabled(1);
    assert(stock[32] == (original[32] ^ 0xaa));
    assert(!theme && !allocations && cfw_assets_enabled());
    unsigned char dark[sizeof(stock)];
    memcpy(dark, stock, sizeof(stock));
    pack(0);
    missing = 1;
    rejected(dark);
    cfw_assets_set_enabled(0);
    assert(cfw_assets_enabled() && !memcmp(stock, dark, sizeof(stock)));
    missing = 0;
    short_read = 1;
    rejected(dark);
    short_read = 0;
    no_memory = 1;
    rejected(dark);
    no_memory = 0;

    struct theme_header *header = (void *)disk.data;
    struct theme_record *record = (void *)(disk.data + sizeof(*header));
    record->data[0] ^= 1;
    header->crc = ~checksum(UINT32_MAX, (const void *)record, header->size);
    rejected(dark);
    pack(0);
    stock[0] ^= 1;
    assert(!cfw_assets_prepare() && !allocations && cfw_assets_enabled());
    stock[0] ^= 1;

    assert(cfw_assets_prepare() && allocations == 1);
    cfw_assets_cancel();
    assert(!allocations && cfw_assets_enabled() && !memcmp(stock, dark, sizeof(stock)));
    assert(cfw_assets_prepare() && allocations == 1);
    cfw_assets_set_enabled(0);
    assert(!memcmp(stock, original, sizeof(stock)) && !allocations && !theme);
    assert(!cfw_assets_enabled());
    pack(1);
    assert(cfw_assets_prepare());
    cfw_assets_set_enabled(1);
    assert(!allocations && !memcmp(stock, dark, sizeof(stock)));
    pack(0);
    assert(cfw_assets_prepare());
    cfw_assets_set_enabled(0);
    assert(!allocations && !memcmp(stock, original, sizeof(stock)));

    pack(1);
    short_read = 1;
    rejected(original);
    short_read = 0;
    no_memory = 1;
    rejected(original);
    no_memory = 0;

    for (unsigned byte = 0; byte < disk_size; byte++) {
        unsigned char saved = disk.data[byte];
        disk.data[byte] ^= 0x80;
        rejected(original);
        disk.data[byte] = saved;
    }

    for (unsigned length = 0; length < disk_size; length++) {
        uint32_t saved = disk_size;
        disk_size = length;
        rejected(original);
        disk_size = saved;
    }

    for (unsigned kind = 0; kind < 6; kind++) {
        pack(1);
        if (kind == 0)
            record->asset = 1;
        if (kind == 1)
            record->offset = 0;
        if (kind == 2)
            record->size = UINT32_MAX;
        if (kind == 3)
            record->offset = sizeof(stock);
        if (kind == 4)
            header->count = 0;
        if (kind == 5) {
            record->offset = 40;
            record->size = 1;
            record->data[0] = 3;
        }
        header->crc = ~checksum(UINT32_MAX, (const void *)record, header->size);
        rejected(original);
    }

    pack(1);
    record->data[8] = 2;
    header->crc = ~checksum(UINT32_MAX, (const void *)record, header->size);
    rejected(original);

    pack(1);
    stock[0] ^= 1;
    assert(!cfw_assets_prepare() && !allocations);
    stock[0] ^= 1;
    assert(cfw_assets_prepare());
    cfw_assets_cancel();
    assert(!allocations && !memcmp(stock, original, sizeof(stock)));

    assert(cfw_bitmap_resource_get(NULL, 0x424d6170, 7) == stock);
    assert(cfw_bitmap_resource_get(NULL, OSOS_RESOURCE_COLR, 7) == stock + 1);
    return 0;
}
