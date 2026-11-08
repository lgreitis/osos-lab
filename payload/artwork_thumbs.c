/* SPDX-License-Identifier: GPL-3.0-only */

#include "artwork_thumbs.h"
#include "osos.h"
#include "patch.h"

/*
 * iTunes writes one ArtworkDB image per track, each with its own chain of thumbnail
 * records, even when every track on an album has the same cover. The native importer
 * keeps all of them in heap. While the ArtworkDB loads, an image whose chain matches
 * one already loaded is dropped: its thumbnails and its image record go back to their
 * pools, and the iTunesDB load then gives its tracks the ID of the first image.
 *
 * Sharing is safe because the ArtworkDB never changes after import: readers only
 * walk an image's chain, the per-thumbnail destructor runs only for photo databases,
 * and teardown frees the pools as a whole. A shared thumbnail's image pointer names
 * the first image, which has the same database and name cache.
 *
 * Every track reaches its image by ID, or by DBID when it has no ID. Dropped IDs
 * are rewritten as the mhit parser stores them, before anything reads them. A
 * dropped image's DBID stays resolvable only if a track without an ID needs it.
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

struct duplicate {
    uint32_t id;
    void *image;
    /* DBIDs are 32-bit aligned; ldrd would need 64-bit alignment. */
    uint32_t dbid[2];
};

/* The image pool: chunks of fixed-size slots, each chunk with its own free list. */
struct pool_chunk {
    uint32_t magic;
    void *pool;
    struct pool_chunk *next;
    uint32_t slots, free;
    uint8_t *first, *last;
    void *free_list;
};

struct pool {
    uint32_t magic, slot_size, slots;
    struct pool_chunk *chunks;
};

_Static_assert(sizeof(void *) != 4 || sizeof(struct pool_chunk) == 0x20, "Pool chunk");

#define INITIAL_DUPLICATES 256u

static void *active_db;
static struct canonical *entries;
static uint32_t capacity, used;
static int disabled;
static void *last_kept;

/* Dropped images in load order; after the load, orders over them by ID and by DBID. */
static struct duplicate *duplicates;
static uint32_t *by_id, *by_dbid;
static uint32_t duplicate_count, duplicate_capacity;
static void *duplicate_db;
static int remapping, dbid_needed;

static struct artwork_thumb **image_thumbs(void *image)
{
    return (struct artwork_thumb **)((uint8_t *)image +
                                     OSOS_ARTWORK_IMAGE_THUMBS_OFFSET);
}

static uint16_t *image_thumb_count(void *image)
{
    return (uint16_t *)((uint8_t *)image + OSOS_ARTWORK_IMAGE_THUMB_COUNT_OFFSET);
}

#define FIELD(type, base, offset) (*(type *)((uint8_t *)(base) + (offset)))

static uint64_t dbid_at(void *base, uint32_t offset)
{
    return (uint64_t)FIELD(uint32_t, base, offset + 4) << 32 |
           FIELD(uint32_t, base, offset);
}

static uint64_t key(uint32_t index, int dbid)
{
    return dbid ? dbid_at(duplicates[index].dbid, 0) : duplicates[index].id;
}

static void sift(uint32_t *order, uint32_t root, uint32_t count, int dbid)
{
    for (;;) {
        uint32_t child = root * 2 + 1;
        if (child >= count)
            return;
        if (child + 1 < count && key(order[child], dbid) < key(order[child + 1], dbid))
            child++;
        if (key(order[root], dbid) >= key(order[child], dbid))
            return;
        uint32_t swap = order[root];
        order[root] = order[child];
        order[child] = swap;
        root = child;
    }
}

static void sort(uint32_t *order, int dbid)
{
    uint32_t count = duplicate_count;
    for (uint32_t i = count / 2; i-- > 0;)
        sift(order, i, count, dbid);
    for (uint32_t i = count; i-- > 1;) {
        uint32_t swap = order[0];
        order[0] = order[i];
        order[i] = swap;
        sift(order, 0, i, dbid);
    }
}

static struct duplicate *find(const uint32_t *order, int dbid, uint64_t value)
{
    uint32_t low = 0, high = duplicate_count;
    while (low < high) {
        uint32_t middle = (low + high) / 2;
        uint64_t k = key(order[middle], dbid);
        if (k == value)
            return &duplicates[order[middle]];
        if (k < value)
            low = middle + 1;
        else
            high = middle;
    }
    return NULL;
}

static void forget_duplicates(void)
{
    if (duplicates)
        osos_import_free(duplicates);
    duplicates = NULL;
    by_id = by_dbid = NULL;
    duplicate_count = duplicate_capacity = 0;
    duplicate_db = NULL;
    remapping = dbid_needed = 0;
}

/* One allocation holds the records and both orders. */
static int grow_duplicates(void)
{
    if (duplicate_count < duplicate_capacity)
        return 1;
    uint32_t size = duplicate_capacity ? duplicate_capacity * 2 : INITIAL_DUPLICATES;
    if (size > UINT32_MAX / (sizeof(struct duplicate) + 8))
        return 0;
    struct duplicate *table = osos_import_alloc(size * (sizeof(struct duplicate) + 8));
    if (!table)
        return 0;
    for (uint32_t i = 0; i < duplicate_count; i++)
        table[i] = duplicates[i];
    if (duplicates)
        osos_import_free(duplicates);
    duplicates = table;
    by_id = (uint32_t *)(table + size);
    by_dbid = by_id + size;
    duplicate_capacity = size;
    return 1;
}

/*
 * Removes a just-loaded duplicate image from the database. It is the list tail and
 * the last kept image precedes it; its slot is the next one the pool hands out.
 */
static int drop(void *image, void *canonical)
{
    void *db = active_db;
    if (!last_kept || FIELD(void *, last_kept, 0) != image || FIELD(void *, image, 0) ||
        FIELD(void *, db, OSOS_ARTWORK_DB_LAST_IMAGE_OFFSET) != image)
        return 0;
    struct pool_chunk *chunk =
        FIELD(struct pool *, db, OSOS_ARTWORK_DB_IMAGE_POOL_OFFSET)->chunks;
    while (chunk && ((uint8_t *)image < chunk->first || (uint8_t *)image > chunk->last))
        chunk = chunk->next;
    if (!chunk || !grow_duplicates())
        return 0;
    struct duplicate *d = &duplicates[duplicate_count];
    d->id = FIELD(uint32_t, image, OSOS_ARTWORK_IMAGE_ID_OFFSET);
    d->image = canonical;
    d->dbid[0] = FIELD(uint32_t, image, OSOS_ARTWORK_IMAGE_DBID_OFFSET);
    d->dbid[1] = FIELD(uint32_t, image, OSOS_ARTWORK_IMAGE_DBID_OFFSET + 4);
    duplicate_count++;
    FIELD(void *, last_kept, 0) = NULL;
    FIELD(void *, db, OSOS_ARTWORK_DB_LAST_IMAGE_OFFSET) = last_kept;
    FIELD(uint32_t, db, OSOS_ARTWORK_DB_IMAGE_COUNT_OFFSET)--;
    FIELD(void *, image, 0) = chunk->free_list;
    chunk->free_list = image;
    chunk->free++;
    return 1;
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
    active_db = last_kept = NULL;
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

/* Returns whether the image was dropped. */
static int share(void *image)
{
    struct artwork_thumb *thumbs = *image_thumbs(image);
    if (!thumbs)
        return 0;
    if (!grow()) {
        disabled = 1;
        return 0;
    }
    uint32_t hash = chain_hash(thumbs);
    struct canonical *entry = slot(entries, capacity, hash, image);
    if (!entry->image) {
        entry->hash = hash;
        entry->image = image;
        used++;
        return 0;
    }
    /* The destructor unlinks each thumbnail from this image and returns it to the pool. */
    while (thumbs) {
        struct artwork_thumb *next = thumbs->next;
        osos_artwork_thumb_destroy(thumbs);
        thumbs = next;
    }
    *image_thumbs(image) = *image_thumbs(entry->image);
    *image_thumb_count(image) = *image_thumb_count(entry->image);
    return drop(image, entry->image);
}

PATCH_ARM int cfw_artwork_import(void *db, uint32_t owner, const void *path,
                                 uint32_t a3, uint32_t a4, uint32_t a5)
{
    clear();
    forget_duplicates();
    active_db = db;
    int status = osos_artwork_import(db, owner, path, a3, a4, a5);
    clear();
    if (status || !duplicate_count) {
        forget_duplicates();
    } else {
        for (uint32_t i = 0; i < duplicate_count; i++)
            by_id[i] = by_dbid[i] = i;
        sort(by_id, 0);
        sort(by_dbid, 1);
        duplicate_db = db;
        remapping = 1;
    }
    return status;
}

PATCH_ARM int cfw_artwork_image_thumbs(void *reader, void *image, uint32_t count,
                                       uint32_t flags)
{
    int status = osos_artwork_image_thumbs(reader, image, count, flags);
    void *db = FIELD(void *, reader, OSOS_ARTWORK_READER_DB_OFFSET);
    if (!status && active_db && db == active_db && (disabled || !share(image)))
        last_kept = image;
    return status;
}

/* ArtworkDB photo albums name images by ID while the database loads. */
PATCH_ARM void *cfw_artwork_album_image(void *db, uint32_t id)
{
    void *image = osos_artwork_find_image(db, id);
    for (uint32_t i = 0; !image && db == active_db && i < duplicate_count; i++) {
        if (duplicates[i].id == id)
            image = duplicates[i].image;
    }
    return image;
}

/* Called by cfw_artwork_track_hook in place of the parser's store of the image ID. */
void cfw_artwork_track_image(uint32_t id, void *track)
{
    if (remapping) {
        struct duplicate *d;
        if (id) {
            if ((d = find(by_id, 0, id)))
                id = FIELD(uint32_t, d->image, OSOS_ARTWORK_IMAGE_ID_OFFSET);
        } else if (find(by_dbid, 1, dbid_at(track, OSOS_TRACK_DBID_OFFSET))) {
            dbid_needed = 1;
        }
    }
    FIELD(uint32_t, track, OSOS_TRACK_ARTWORK_ID_OFFSET) = id;
}

void cfw_artwork_tracks_imported(void)
{
    remapping = 0;
    if (!dbid_needed)
        forget_duplicates();
}

PATCH_ARM void *cfw_artwork_image_by_dbid(void *db, uint32_t a1, uint32_t dbid_low,
                                          uint32_t dbid_high, uint32_t sorted)
{
    void *image = osos_artwork_find_image_by_dbid(db, a1, dbid_low, dbid_high, sorted);
    if (!image && db && db == duplicate_db) {
        struct duplicate *d = find(by_dbid, 1, (uint64_t)dbid_high << 32 | dbid_low);
        if (d)
            image = d->image;
    }
    return image;
}
