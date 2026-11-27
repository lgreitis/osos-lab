/* SPDX-License-Identifier: MIT */

/* The pictures eapp-pack writes, shared by the bitmap, pack and GL loaders;
 * without the symbols header, since gl.c cannot include it. */

#ifndef EAPP_PICTURE_H
#define EAPP_PICTURE_H

#include <stddef.h>
#include <stdint.h>

/* A picture file or a pack entry: "EIMG", width, height, format and colour
 * key as little-endian words, then the pixels row by row. */
struct eapp_picture_header {
    char magic[4];
    uint32_t width, height, format;
    int32_t key;
};

enum eapp_picture_format {
    EAPP_PICTURE_565 = 0,
    EAPP_PICTURE_4444 = 1,
    EAPP_PICTURE_5551 = 2,
    EAPP_PICTURE_8888 = 3,
    EAPP_PICTURE_A8 = 4,
    EAPP_PICTURE_FORMATS = 5,
};

/* The pixel bytes of a picture with that header, or 0 for a header the
 * runtime does not take. */
size_t eapp_picture_bytes(const struct eapp_picture_header *header);

/* Entry `index` of a loaded pack: its header and pixels, or NULL past the
 * end. */
struct eapp_pack;
const struct eapp_picture_header *eapp_pack_entry(const struct eapp_pack *pack, unsigned index,
                                                  const void **pixels);

#endif
