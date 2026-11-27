/* SPDX-License-Identifier: MIT */

/* A picture of the package as eapp-pack writes it, loaded into one heap
 * block: the bitmap struct, then the pixels. */

#include "eapp_draw.h"
#include "picture.h"

struct loaded_bitmap {
    struct eapp_bitmap bitmap;
    uint16_t pixels[];
};

_Static_assert(offsetof(struct loaded_bitmap, pixels) >= sizeof(struct eapp_picture_header),
               "the header must fit before the pixels");

struct eapp_bitmap *eapp_bitmap_load(const char *path)
{
    struct eapp_picture_header header;
    int size = eapp_file_read_all(path, &header, sizeof header);
    size_t bytes = size >= (int)sizeof header ? eapp_picture_bytes(&header) : 0;
    if (!bytes || header.format != EAPP_PICTURE_565 || (size_t)size != sizeof header + bytes)
        return NULL;
    struct loaded_bitmap *loaded = eapp_alloc(sizeof *loaded + bytes);
    if (!loaded)
        return NULL;
    /* The whole file again, header included, into the block's tail so the
     * pixels land in place: the header fits in the struct before them. */
    uint8_t *file = (uint8_t *)loaded->pixels - sizeof header;
    if (eapp_file_read_all(path, file, sizeof header + bytes) != size) {
        eapp_free(loaded);
        return NULL;
    }
    loaded->bitmap = (struct eapp_bitmap){loaded->pixels, header.width, header.height,
                                          header.width, header.key};
    return &loaded->bitmap;
}

void eapp_bitmap_free(struct eapp_bitmap *bitmap)
{
    eapp_free(bitmap);
}
