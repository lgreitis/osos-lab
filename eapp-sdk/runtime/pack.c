/* SPDX-License-Identifier: MIT */

/* Picture packs: "EPAK", the count, then an offset per entry from the start
 * of the file, as little-endian words; each entry a picture with its header,
 * 4-byte aligned. The whole file is read in one request into one block,
 * which also holds a bitmap per entry over the pixels in place. */

#include "eapp_draw.h"
#include "eapp_libc.h"
#include "picture.h"

#define PACK_MAX_BYTES (4u << 20)
#define PACK_MAX_ENTRIES 1024

struct eapp_pack {
    uint32_t count;
    uint32_t bytes;
    struct eapp_bitmap *bitmaps; /* One per entry; pixels NULL for a non-565 one. */
    uint8_t *file;
};

static const size_t pixel_bytes[EAPP_PICTURE_FORMATS] = {2, 2, 2, 4, 1};

size_t eapp_picture_bytes(const struct eapp_picture_header *header)
{
    if (memcmp(header->magic, "EIMG", 4) || header->format >= EAPP_PICTURE_FORMATS ||
        !header->width || !header->height || header->width > 4096 || header->height > 4096)
        return 0;
    return (size_t)header->width * header->height * pixel_bytes[header->format];
}

struct eapp_pack *eapp_pack_load(const char *path)
{
    uint32_t head[2];
    int size = eapp_file_read_all(path, head, sizeof head);
    if (size < (int)sizeof head || memcmp(head, "EPAK", 4) || head[1] == 0 ||
        head[1] > PACK_MAX_ENTRIES || (size_t)size > PACK_MAX_BYTES)
        return NULL;
    uint32_t count = head[1];
    size_t table = 8 + (size_t)count * 4;
    if ((size_t)size < table)
        return NULL;
    size_t bitmaps = sizeof(struct eapp_bitmap) * count;
    struct eapp_pack *pack = eapp_alloc(sizeof *pack + bitmaps + (size_t)size);
    if (!pack)
        return NULL;
    pack->count = count;
    pack->bytes = (uint32_t)size;
    pack->bitmaps = (struct eapp_bitmap *)(pack + 1);
    pack->file = (uint8_t *)pack->bitmaps + bitmaps;
    if (eapp_file_read_all(path, pack->file, (size_t)size) != size) {
        eapp_free(pack);
        return NULL;
    }
    for (unsigned i = 0; i < count; ++i) {
        const void *pixels;
        const struct eapp_picture_header *header = eapp_pack_entry(pack, i, &pixels);
        if (!header) {
            eapp_free(pack);
            return NULL;
        }
        pack->bitmaps[i] = (struct eapp_bitmap){
            header->format == EAPP_PICTURE_565 ? pixels : NULL, header->width, header->height,
            header->width, header->key};
    }
    return pack;
}

const struct eapp_picture_header *eapp_pack_entry(const struct eapp_pack *pack, unsigned index,
                                                  const void **pixels)
{
    if (!pack || index >= pack->count)
        return NULL;
    uint32_t offset;
    memcpy(&offset, pack->file + 8 + (size_t)index * 4, 4);
    if (offset % 4 || offset > pack->bytes ||
        pack->bytes - offset < sizeof(struct eapp_picture_header))
        return NULL;
    const struct eapp_picture_header *header =
        (const struct eapp_picture_header *)(pack->file + offset);
    size_t bytes = eapp_picture_bytes(header);
    if (!bytes || pack->bytes - offset - sizeof *header < bytes)
        return NULL;
    *pixels = header + 1;
    return header;
}

unsigned eapp_pack_count(const struct eapp_pack *pack)
{
    return pack ? pack->count : 0;
}

const struct eapp_bitmap *eapp_pack_bitmap(const struct eapp_pack *pack, unsigned index)
{
    if (!pack || index >= pack->count || !pack->bitmaps[index].pixels)
        return NULL;
    return &pack->bitmaps[index];
}

void eapp_pack_free(struct eapp_pack *pack)
{
    eapp_free(pack);
}
