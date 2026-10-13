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
const struct cfw_bitmap_asset cfw_bitmap_assets[] = {
    {stock, sizeof(stock), 32, 40, 2, 2, 2, 1, 8, 0x25789516}};
const uint32_t cfw_bitmap_asset_count = 1, cfw_theme_max_bytes = 24;
const unsigned char cfw_theme_catalog_hash[32] = {1, 2, 3};

static union {
    uint32_t alignment;
    unsigned char data[128];
} disk;

static uint32_t disk_size, cursor;
static int missing, short_read, no_memory, allocations, opens;

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
    assert(path->volume == 0 && !strcmp(path->text, "themes\\dark\\theme.bin") &&
           mode == 1);
    opens++;
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

const void *cfw_theme_color(uint32_t id, const void *color)
{
    assert(id == 7 && color == stock);
    return stock + 1;
}

#include "../../../payload/assets.c"

static void pack(void)
{
    memset(disk.data, 0, sizeof(disk.data));
    struct theme_header *header = (void *)disk.data;
    header->magic = 0x31485452;
    memcpy(header->catalog, cfw_theme_catalog_hash, 32);
    header->count = 1;
    header->size = 16;
    struct theme_record *record = (void *)(disk.data + sizeof(*header));
    record->offset = 32;
    record->size = 4;
    record->delta[0] = 0xaa;
    header->crc = checksum((const void *)record, header->size);
    disk_size = sizeof(*header) + header->size;
}

static void rejected(const unsigned char *original)
{
    assert(!cfw_assets_prepare());
    assert(!theme && !allocations && !active);
    assert(!memcmp(stock, original, sizeof(stock)));
}

int main(void)
{
    unsigned char original[sizeof(stock)];
    memcpy(original, stock, sizeof(stock));
    assert(checksum(stock, sizeof(stock)) == cfw_bitmap_assets[0].crc);
    for (unsigned cycle = 0; cycle < 3; cycle++) {
        pack();
        assert(cfw_assets_prepare() && allocations == 1);
        assert(!memcmp(stock, original, sizeof(stock)));
        cfw_assets_set_enabled(1);
        assert(stock[32] == (original[32] ^ 0xaa));
        assert(!memcmp(stock + 40, original + 40, 2));
        cfw_assets_set_enabled(1);
        assert(stock[32] == (original[32] ^ 0xaa));
        int previous_opens = opens;
        missing = 1;
        assert(cfw_assets_prepare());
        cfw_assets_set_enabled(0);
        assert(!memcmp(stock, original, sizeof(stock)) && !allocations);
        assert(opens == previous_opens);
        rejected(original);
        missing = 0;
    }

    pack();
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

    struct theme_header *header = (void *)disk.data;
    struct theme_record *record = (void *)(disk.data + sizeof(*header));
    for (unsigned kind = 0; kind < 6; kind++) {
        pack();
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
            record->delta[0] = 3;
        }
        header->crc = checksum((const void *)record, header->size);
        rejected(original);
    }

    pack();
    stock[0] ^= 1;
    assert(!cfw_assets_prepare() && !allocations);
    stock[0] ^= 1;
    assert(cfw_assets_prepare());
    cfw_assets_cancel();
    assert(!allocations && !memcmp(stock, original, sizeof(stock)));

    pack();
    assert(cfw_assets_prepare());
    cfw_assets_set_enabled(1);
    pack();
    record->delta[0] = 0xbb;
    header->crc = checksum((const void *)record, header->size);
    assert(cfw_assets_prepare_path(&theme_path) && allocations == 2);
    assert(stock[32] == (original[32] ^ 0xaa));
    cfw_assets_cancel();
    assert(allocations == 1 && stock[32] == (original[32] ^ 0xaa));

    header->crc ^= 1;
    assert(!cfw_assets_prepare_path(&theme_path) && allocations == 1);
    assert(stock[32] == (original[32] ^ 0xaa));

    header->crc ^= 1;
    assert(cfw_assets_prepare_path(&theme_path));
    cfw_assets_set_enabled(1);
    assert(allocations == 1 && stock[32] == (original[32] ^ 0xbb));
    cfw_assets_set_enabled(0);
    assert(!allocations && !memcmp(stock, original, sizeof(stock)));

    pack();
    memmove(disk.data + sizeof(*header) + sizeof(struct cfw_theme_metadata),
            disk.data + sizeof(*header), header->size);
    struct cfw_theme_metadata *metadata = (void *)(disk.data + sizeof(*header));
    memset(metadata, 0, sizeof(*metadata));
    metadata->accent = 0xb57be8;
    strcpy(metadata->name, "Dark Purple");
    header->magic = 0x32485452;
    disk_size += sizeof(*metadata);
    struct cfw_theme_metadata found;
    assert(cfw_assets_metadata(&theme_path, &found) && !allocations);
    assert(found.accent == metadata->accent && !strcmp(found.name, "Dark Purple"));
    assert(cfw_assets_prepare());
    cfw_assets_set_enabled(1);
    assert(cfw_assets_enabled() && cfw_assets_accent() == 0xb57be8);
    cfw_assets_set_enabled(0);
    assert(!cfw_assets_accent() && !allocations &&
           !memcmp(stock, original, sizeof(stock)));

    metadata->accent = 0xff000000;
    rejected(original);
    metadata->accent = 0;
    memset(metadata->name, 'x', sizeof(metadata->name));
    rejected(original);
    assert(cfw_bitmap_resource_get(NULL, 0x424d6170, 7) == stock);
    assert(cfw_bitmap_resource_get(NULL, OSOS_RESOURCE_COLR, 7) == stock + 1);
    return 0;
}
