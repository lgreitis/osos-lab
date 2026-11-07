/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"
#include "patch.h"

/*
 * iTunes writes one ArtworkDB image per track, each with its own chain of thumbnail
 * records, even when every track on an album has the same cover. The native importer
 * keeps all of them in heap. While the ArtworkDB loads, an image whose chain matches
 * one already loaded gets that chain instead, and its own thumbnails go back to the
 * pool.
 *
 * Sharing is safe because the ArtworkDB never changes after import: readers only
 * walk an image's chain, the per-thumbnail destructor runs only for photo databases,
 * and teardown frees the thumbnail pool as a whole. A shared thumbnail's image
 * pointer names the first image, which has the same database and name cache.
 */

struct artwork_thumb {
    struct artwork_thumb *next;
    void *image;
    uint32_t format;
    uint32_t dimensions;
    uint16_t padding[4];
    uint32_t offset;
    uint32_t size;
    /* A token in the name cache that osos_artwork_thumb_name_cache returns. */
    uint32_t file;
};

_Static_assert(sizeof(void *) != 4 || sizeof(struct artwork_thumb) == 0x24,
               "Artwork thumbnail layout");

struct canonical {
    uint32_t hash;
    void *image;
};

#define INITIAL_CAPACITY 1024u

static void *active_db;
static struct canonical *entries;
static uint32_t capacity, used;
static int disabled;

static struct artwork_thumb **image_thumbs(void *image)
{
    return (struct artwork_thumb **)((uint8_t *)image +
                                     OSOS_ARTWORK_IMAGE_THUMBS_OFFSET);
}

static uint16_t *image_thumb_count(void *image)
{
    return (uint16_t *)((uint8_t *)image + OSOS_ARTWORK_IMAGE_THUMB_COUNT_OFFSET);
}

static uint32_t mix(uint32_t hash, uint32_t value)
{
    hash ^= value;
    hash *= 0x01000193;
    return hash ^ (hash >> 15);
}

static int same_thumb(struct artwork_thumb *a, struct artwork_thumb *b)
{
    return a->format == b->format && a->dimensions == b->dimensions &&
           a->padding[0] == b->padding[0] && a->padding[1] == b->padding[1] &&
           a->padding[2] == b->padding[2] && a->padding[3] == b->padding[3] &&
           a->offset == b->offset && a->size == b->size && a->file == b->file &&
           osos_artwork_thumb_name_cache(a) == osos_artwork_thumb_name_cache(b);
}

static uint32_t chain_hash(struct artwork_thumb *thumb)
{
    uint32_t hash = 0x811c9dc5;
    for (; thumb; thumb = thumb->next) {
        hash = mix(hash, thumb->format);
        hash = mix(hash, thumb->dimensions);
        hash =
            mix(hash, (uint32_t)thumb->padding[0] | (uint32_t)thumb->padding[1] << 16);
        hash =
            mix(hash, (uint32_t)thumb->padding[2] | (uint32_t)thumb->padding[3] << 16);
        hash = mix(hash, thumb->offset);
        hash = mix(hash, thumb->size);
        hash = mix(hash, thumb->file);
        hash = mix(hash, (uint32_t)(uintptr_t)osos_artwork_thumb_name_cache(thumb));
    }
    return hash;
}

static int same_chain(struct artwork_thumb *a, struct artwork_thumb *b)
{
    for (; a && b; a = a->next, b = b->next) {
        if (!same_thumb(a, b))
            return 0;
    }
    return !a && !b;
}

static struct canonical *slot(struct canonical *table, uint32_t size, uint32_t hash,
                              void *image)
{
    uint32_t index = hash & (size - 1);
    while (table[index].image &&
           (table[index].hash != hash ||
            (image &&
             !same_chain(*image_thumbs(table[index].image), *image_thumbs(image)))))
        index = (index + 1) & (size - 1);
    return &table[index];
}

static void clear(void)
{
    if (entries)
        osos_import_free(entries);
    entries = NULL;
    capacity = used = 0;
    active_db = NULL;
    disabled = 0;
}

static int grow(void)
{
    if (capacity && used < capacity * 3 / 4)
        return 1;
    if (capacity > UINT32_MAX / 2 / sizeof(*entries))
        return 0;
    uint32_t size = capacity ? capacity * 2 : INITIAL_CAPACITY;
    struct canonical *table = osos_import_alloc(size * sizeof(*table));
    if (!table)
        return 0;
    for (uint32_t i = 0; i < size; i++) {
        table[i].hash = 0;
        table[i].image = NULL;
    }
    /* Canonical chains are distinct, so rehashing needs no chain comparison. */
    for (uint32_t i = 0; i < capacity; i++) {
        if (entries[i].image)
            *slot(table, size, entries[i].hash, NULL) = entries[i];
    }
    if (entries)
        osos_import_free(entries);
    entries = table;
    capacity = size;
    return 1;
}

static void share(void *image)
{
    struct artwork_thumb *thumbs = *image_thumbs(image);
    if (!thumbs)
        return;
    if (!grow()) {
        disabled = 1;
        return;
    }
    uint32_t hash = chain_hash(thumbs);
    struct canonical *entry = slot(entries, capacity, hash, image);
    if (!entry->image) {
        entry->hash = hash;
        entry->image = image;
        used++;
        return;
    }
    /* The destructor unlinks each thumbnail from this image and returns it to the pool. */
    while (thumbs) {
        struct artwork_thumb *next = thumbs->next;
        osos_artwork_thumb_destroy(thumbs);
        thumbs = next;
    }
    *image_thumbs(image) = *image_thumbs(entry->image);
    *image_thumb_count(image) = *image_thumb_count(entry->image);
}

PATCH_ARM int cfw_artwork_import(void *db, uint32_t owner, const void *path,
                                 uint32_t a3, uint32_t a4, uint32_t a5)
{
    clear();
    active_db = db;
    int status = osos_artwork_import(db, owner, path, a3, a4, a5);
    clear();
    return status;
}

PATCH_ARM int cfw_artwork_image_thumbs(void *reader, void *image, uint32_t count,
                                       uint32_t flags)
{
    int status = osos_artwork_image_thumbs(reader, image, count, flags);
    void *db = *(void **)((uint8_t *)reader + OSOS_ARTWORK_READER_DB_OFFSET);
    if (!status && active_db && db == active_db && !disabled)
        share(image);
    return status;
}
