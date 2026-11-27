/* SPDX-License-Identifier: MIT */

/* Covers and photos through the host's Metadata interface. An image has a
 * thumbnail per artwork format; the host describes a format from a table
 * and stops the iPod for an id the table lacks, so the runtime only asks
 * about the ids the table holds, and draws only plain RGB565. */

#include "osos-2.0.5.h"
#include "platform.h"
#include "eapp_draw.h"

#define HANDLE EAPP_MEDIA_HANDLE
#define OBJECT EAPP_MEDIA_OBJECT

static const int16_t formats[] = {
    ArtworkFormatId_1009, ArtworkFormatId_1013, ArtworkFormatId_1015, ArtworkFormatId_1016,
    ArtworkFormatId_1017, ArtworkFormatId_1020, ArtworkFormatId_1021, ArtworkFormatId_1023,
    ArtworkFormatId_1024, ArtworkFormatId_1027, ArtworkFormatId_1028, ArtworkFormatId_1029,
    ArtworkFormatId_1031, ArtworkFormatId_1032, ArtworkFormatId_1035, ArtworkFormatId_1036,
    ArtworkFormatId_1050, ArtworkFormatId_1051, ArtworkFormatId_1052, ArtworkFormatId_1053,
    ArtworkFormatId_1054, ArtworkFormatId_1055, ArtworkFormatId_1060, ArtworkFormatId_1061,
    ArtworkFormatId_1065, ArtworkFormatId_1066, ArtworkFormatId_1068, ArtworkFormatId_1069,
};

static int32_t artwork_library = -1, photo_library = -1;

static int32_t open_artwork_library(void)
{
    if (artwork_library < 0)
        artwork_library = GameMetadata_ArtworkLibraryCreate();
    return artwork_library;
}

static int32_t open_photo_library(void)
{
    if (photo_library < 0)
        photo_library = GameMetadata_PhotoLibraryCreate();
    return photo_library;
}

struct eapp_media_image *eapp_media_artwork_find(uint64_t track_id)
{
    if (open_artwork_library() < 0)
        return NULL;
    return OBJECT(struct eapp_media_image,
                  GameMetadata_ArtworkLibraryGetTrackImage(artwork_library, track_id));
}

struct eapp_media_image *eapp_media_album_artwork_open(uint32_t index)
{
    int32_t library = eapp_media_library(EAPP_MEDIA_ALBUMS, index);
    if (library < 0)
        return NULL;
    return OBJECT(struct eapp_media_image, GameMetadata_LibraryGetAlbumImage(library, index));
}

void eapp_media_image_close(struct eapp_media_image *image)
{
    if (image)
        GameMetadata_ImageDestroy(HANDLE(image));
}

struct format {
    uint32_t width, height, row_bytes;
};

static uint32_t word(const uint8_t *record, unsigned offset)
{
    uint32_t value;
    memcpy(&value, record + offset, sizeof value);
    return value;
}

/* The format's size when its pixels are plain 16-bit RGB565. */
static int drawable(int16_t id, struct format *out)
{
    _Alignas(4) uint8_t record[GameImageFormat_bytes];
    GameMetadata_ArtworkLibraryGetFormat((struct GameImageFormat *)record, artwork_library, id);
    out->width = word(record, GameImageFormat_width);
    out->height = word(record, GameImageFormat_height);
    out->row_bytes = word(record, GameImageFormat_row_bytes);
    return word(record, GameImageFormat_bits_per_pixel) == 16 &&
           record[GameImageFormat_pixel_format] == GameImagePixelFormat_RGB565 &&
           out->width && out->height && out->row_bytes >= out->width * 2 && !(out->row_bytes & 1);
}

struct eapp_media_picture {
    int32_t data;
    struct eapp_bitmap bitmap;
};

struct eapp_media_picture *eapp_media_image_load(const struct eapp_media_image *image,
                                                 uint32_t max_width, uint32_t max_height)
{
    if (!image || open_artwork_library() < 0)
        return NULL;
    int32_t handle = HANDLE(image);
    int best = -1, smallest = -1;
    struct format best_format = {0}, smallest_format = {0};
    for (unsigned i = 0; i < sizeof formats / sizeof formats[0]; ++i) {
        struct format format;
        if (GameMetadata_ImageCountThumbs(handle, formats[i]) <= 0 ||
            !drawable(formats[i], &format))
            continue;
        uint32_t area = format.width * format.height;
        if (format.width <= max_width && format.height <= max_height &&
            (best < 0 || area > best_format.width * best_format.height)) {
            best = (int)i;
            best_format = format;
        }
        if (smallest < 0 || area < smallest_format.width * smallest_format.height) {
            smallest = (int)i;
            smallest_format = format;
        }
    }
    if (best < 0) {
        best = smallest;
        best_format = smallest_format;
    }
    if (best < 0)
        return NULL;
    struct eapp_media_picture *picture = eapp_alloc(sizeof *picture);
    if (!picture)
        return NULL;
    picture->data = GameMetadata_ImageGetThumbAt(handle, formats[best], 1, 0);
    /* Before a successful load the pixel and size entries answer garbage. */
    const uint16_t *pixels = picture->data >= 0 &&
                                     GameMetadata_ImageDataGetLoadedHeader(picture->data)
                                 ? GameMetadata_ImageDataGetPixels(picture->data)
                                 : NULL;
    uint32_t bytes = best_format.row_bytes * best_format.height;
    if (!pixels || GameMetadata_ImageDataGetByteCount(picture->data) < bytes) {
        if (picture->data >= 0)
            GameMetadata_ImageDataDestroy(picture->data);
        eapp_free(picture);
        return NULL;
    }
    picture->bitmap = (struct eapp_bitmap){
        .pixels = pixels,
        .width = best_format.width,
        .height = best_format.height,
        .stride_pixels = best_format.row_bytes / 2,
        .transparent = -1,
    };
    return picture;
}

const struct eapp_bitmap *eapp_media_picture_bitmap(const struct eapp_media_picture *picture)
{
    return picture ? &picture->bitmap : NULL;
}

void eapp_media_picture_free(struct eapp_media_picture *picture)
{
    if (!picture)
        return;
    GameMetadata_ImageDataDestroy(picture->data);
    eapp_free(picture);
}

uint32_t eapp_media_photo_album_count(void)
{
    return open_photo_library() >= 0 ? GameMetadata_PhotoLibraryGetAlbumCount(photo_library) : 0;
}

static int32_t open_album(uint32_t album)
{
    if (album >= eapp_media_photo_album_count())
        return -1;
    return GameMetadata_PhotoLibraryGetAlbumAt(photo_library, album);
}

int eapp_media_photo_album_name(uint32_t album, char *out, size_t capacity)
{
    int32_t handle = open_album(album);
    if (handle < 0)
        return -1;
    int length = eapp_media_copy_text(GameMetadata_PhotoAlbumCopyName, handle, out, capacity);
    GameMetadata_PhotoAlbumDestroy(handle);
    return length;
}

int32_t eapp_media_photo_album_index(const char *name)
{
    if (!name || open_photo_library() < 0)
        return -1;
    return GameMetadata_PhotoLibraryFindAlbum(photo_library, name);
}

uint32_t eapp_media_photo_count(int32_t album)
{
    if (album < 0)
        return open_photo_library() >= 0 ? GameMetadata_PhotoLibraryGetImageCount(photo_library)
                                         : 0;
    int32_t handle = open_album((uint32_t)album);
    if (handle < 0)
        return 0;
    uint32_t count = GameMetadata_PhotoAlbumGetImageCount(handle);
    GameMetadata_PhotoAlbumDestroy(handle);
    return count;
}

struct eapp_media_image *eapp_media_photo_open(int32_t album, uint32_t index)
{
    if (index >= eapp_media_photo_count(album))
        return NULL;
    if (album < 0)
        return OBJECT(struct eapp_media_image,
                      GameMetadata_PhotoLibraryGetImageAt(photo_library, index));
    int32_t handle = open_album((uint32_t)album);
    if (handle < 0)
        return NULL;
    int32_t image = GameMetadata_PhotoAlbumGetImageAt(handle, index);
    GameMetadata_PhotoAlbumDestroy(handle);
    return OBJECT(struct eapp_media_image, image);
}

struct eapp_media_image *eapp_media_photo_find(uint32_t id)
{
    if (open_photo_library() < 0)
        return NULL;
    return OBJECT(struct eapp_media_image,
                  GameMetadata_PhotoLibraryGetImageById(photo_library, id));
}

uint32_t eapp_media_image_id(const struct eapp_media_image *image)
{
    return image ? GameMetadata_ImageGetId(HANDLE(image)) : 0;
}
