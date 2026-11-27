/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from RevolaOS, copyright 2026 The RevolaOS Authors.
 */

/* iTunes writes one ArtworkDB image per track, each with its own chain of thumbnail
 * records, even when every track on an album has the same cover, and Apple keeps all
 * of them in heap. While the ArtworkDB loads, an image whose chain matches one
 * already loaded is dropped: its thumbnails and its image record go back to their
 * pools, and the iTunesDB load then gives its tracks the id of the first image.
 *
 * Sharing is safe because the ArtworkDB never changes after import: readers only
 * walk an image's chain, the per-thumbnail destructor runs only for photo databases,
 * and teardown frees the pools as a whole. A shared thumbnail's image pointer names
 * the first image, which has the same database and name cache.
 *
 * Every track reaches its image by id, or by DBID when it has no id. Dropped ids are
 * rewritten as the mhit parser stores them, before anything reads them. A dropped
 * image's DBID stays resolvable only if a track without an id needs it. */

#include "internal.h"
#include "osos.h"
#include "../../rvx-sdk/rv_api.h"
#include "../../rvx-sdk/rv_sites.h"
#include "../../rvx-sdk/osos-2.0.5.h"

#define INITIAL_CAPACITY 1024u
#define INITIAL_DUPLICATES 256u

struct canonical {
    u32 hash;
    void *image;
};

struct duplicate {
    u32 id;
    void *image;
    u32 dbid[2]; /* DBIDs are 32-bit aligned; ldrd would need 64-bit alignment */
};

static void *active_db;
static struct canonical *entries;
static u32 capacity, used;
static int disabled;
static void *last_kept;

/* Dropped images in load order; after the load, orders over them by id and by DBID. */
static struct duplicate *duplicates;
static u32 *by_id, *by_dbid;
static u32 duplicate_count, duplicate_capacity;
static void *duplicate_db;
static int remapping, dbid_needed;

static uint64_t dbid_at(const void *base, u32 offset)
{
    return (uint64_t)RV_FIELD(base, offset + 4, u32) << 32 |
           RV_FIELD(base, offset, u32);
}

static uint64_t key(u32 index, int dbid)
{
    return dbid ? dbid_at(duplicates[index].dbid, 0) : duplicates[index].id;
}

static void sift(u32 *order, u32 root, u32 count, int dbid)
{
    for (;;) {
        u32 child = root * 2 + 1;
        if (child >= count)
            return;
        if (child + 1 < count && key(order[child], dbid) < key(order[child + 1], dbid))
            child++;
        if (key(order[root], dbid) >= key(order[child], dbid))
            return;
        u32 swap = order[root];
        order[root] = order[child];
        order[child] = swap;
        root = child;
    }
}

static void sort(u32 *order, int dbid)
{
    u32 count = duplicate_count;
    for (u32 i = count / 2; i-- > 0;)
        sift(order, i, count, dbid);
    for (u32 i = count; i-- > 1;) {
        u32 swap = order[0];
        order[0] = order[i];
        order[i] = swap;
        sift(order, 0, i, dbid);
    }
}

static struct duplicate *find(const u32 *order, int dbid, uint64_t value)
{
    u32 low = 0, high = duplicate_count;
    while (low < high) {
        u32 middle = (low + high) / 2;
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
    u32 size = duplicate_capacity ? duplicate_capacity * 2 : INITIAL_DUPLICATES;
    if (size > 0xffffffffu / (sizeof(struct duplicate) + 8))
        return 0;
    struct duplicate *table = osos_import_alloc(size * (sizeof(struct duplicate) + 8));
    if (!table)
        return 0;
    for (u32 i = 0; i < duplicate_count; i++)
        table[i] = duplicates[i];
    if (duplicates)
        osos_import_free(duplicates);
    duplicates = table;
    by_id = (u32 *)(table + size);
    by_dbid = by_id + size;
    duplicate_capacity = size;
    return 1;
}

/* Removes a just-loaded duplicate image from the database. It is the list tail and
 * the last kept image precedes it; its slot is the next one the pool hands out. */
static int drop(void *image, void *canonical)
{
    void *db = active_db;
    if (!last_kept || RV_FIELD(last_kept, 0, void *) != image ||
        RV_FIELD(image, 0, void *) ||
        RV_FIELD(db, ArtworkDatabase_last_image, void *) != image)
        return 0;
    uint8_t *chunk = RV_FIELD(RV_FIELD(db, ArtworkDatabase_image_pool, uint8_t *),
                              FixedPool_chunks, uint8_t *);
    while (chunk &&
           ((uint8_t *)image < RV_FIELD(chunk, FixedPoolChunk_first, uint8_t *) ||
            (uint8_t *)image > RV_FIELD(chunk, FixedPoolChunk_last, uint8_t *)))
        chunk = RV_FIELD(chunk, FixedPoolChunk_next, uint8_t *);
    if (!chunk || !grow_duplicates())
        return 0;
    struct duplicate *d = &duplicates[duplicate_count];
    d->id = RV_FIELD(image, ArtworkImage_id, u32);
    d->image = canonical;
    d->dbid[0] = RV_FIELD(image, ArtworkImage_dbid, u32);
    d->dbid[1] = RV_FIELD(image, ArtworkImage_dbid + 4, u32);
    duplicate_count++;
    RV_FIELD(last_kept, 0, void *) = NULL;
    RV_FIELD(db, ArtworkDatabase_last_image, void *) = last_kept;
    RV_FIELD(db, ArtworkDatabase_image_count, u32)--;
    RV_FIELD(image, 0, void *) = RV_FIELD(chunk, FixedPoolChunk_free_list, void *);
    RV_FIELD(chunk, FixedPoolChunk_free_list, void *) = image;
    RV_FIELD(chunk, FixedPoolChunk_free, u32)++;
    return 1;
}

static u32 mix(u32 hash, u32 value)
{
    hash ^= value;
    hash *= 0x01000193;
    return hash ^ (hash >> 15);
}

static u32 thumb_word(const void *thumb, u32 offset)
{
    return RV_FIELD(thumb, offset, u32);
}

static int same_thumb(const void *a, const void *b)
{
    for (u32 offset = ArtworkThumb_format; offset < ArtworkThumb_size + 4; offset += 4)
        if (thumb_word(a, offset) != thumb_word(b, offset))
            return 0;
    return thumb_word(a, ArtworkThumb_file) == thumb_word(b, ArtworkThumb_file) &&
           NATIVE_CALL(ArtworkThumb_GetNameCache)((void *)a) ==
               NATIVE_CALL(ArtworkThumb_GetNameCache)((void *)b);
}

static u32 chain_hash(const void *thumb)
{
    u32 hash = 0x811c9dc5;
    for (; thumb; thumb = RV_FIELD(thumb, ArtworkThumb_next, void *)) {
        for (u32 offset = ArtworkThumb_format; offset <= ArtworkThumb_file; offset += 4)
            hash = mix(hash, thumb_word(thumb, offset));
        hash = mix(hash, (u32)NATIVE_CALL(ArtworkThumb_GetNameCache)((void *)thumb));
    }
    return hash;
}

static int same_chain(const void *a, const void *b)
{
    for (; a && b; a = RV_FIELD(a, ArtworkThumb_next, void *),
                   b = RV_FIELD(b, ArtworkThumb_next, void *))
        if (!same_thumb(a, b))
            return 0;
    return !a && !b;
}

static void *image_thumbs(const void *image)
{
    return RV_FIELD(image, ArtworkImage_thumbs, void *);
}

static struct canonical *slot(struct canonical *table, u32 size, u32 hash,
                              const void *image)
{
    u32 index = hash & (size - 1);
    while (
        table[index].image &&
        (table[index].hash != hash ||
         (image && !same_chain(image_thumbs(table[index].image), image_thumbs(image)))))
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
    if (capacity > 0xffffffffu / 2 / sizeof(*entries))
        return 0;
    u32 size = capacity ? capacity * 2 : INITIAL_CAPACITY;
    struct canonical *table = osos_import_alloc(size * sizeof(*table));
    if (!table)
        return 0;
    /* Canonical chains are distinct, so rehashing needs no chain comparison. */
    for (u32 i = 0; i < capacity; i++)
        if (entries[i].image)
            *slot(table, size, entries[i].hash, NULL) = entries[i];
    if (entries)
        osos_import_free(entries);
    entries = table;
    capacity = size;
    return 1;
}

/* Returns whether the image was dropped. */
static int share(void *image)
{
    void *thumbs = image_thumbs(image);
    if (!thumbs)
        return 0;
    if (!grow()) {
        disabled = 1;
        return 0;
    }
    u32 hash = chain_hash(thumbs);
    struct canonical *entry = slot(entries, capacity, hash, image);
    if (!entry->image) {
        entry->hash = hash;
        entry->image = image;
        used++;
        return 0;
    }
    /* The destructor unlinks each thumbnail from this image and returns it to the
     * pool. */
    while (thumbs) {
        void *next = RV_FIELD(thumbs, ArtworkThumb_next, void *);
        NATIVE_CALL(ArtworkThumb_Destroy)(thumbs);
        thumbs = next;
    }
    RV_FIELD(image, ArtworkImage_thumbs, void *) = image_thumbs(entry->image);
    RV_FIELD(image, ArtworkImage_thumb_count, uint16_t) =
        RV_FIELD(entry->image, ArtworkImage_thumb_count, uint16_t);
    return drop(image, entry->image);
}

static u32 import(u32 db, u32 owner, u32 path, u32 a3, const void *next, u32 a4, u32 a5)
{
    clear();
    forget_duplicates();
    active_db = (void *)db;
    u32 status = rvx_next(next, db, owner, path, a3, a4, a5);
    clear();
    if (status || !duplicate_count) {
        forget_duplicates();
    } else {
        for (u32 i = 0; i < duplicate_count; i++)
            by_id[i] = by_dbid[i] = i;
        sort(by_id, 0);
        sort(by_dbid, 1);
        duplicate_db = (void *)db;
        remapping = 1;
    }
    return status;
}

static u32 image_thumbs_read(u32 reader, u32 image, u32 count, u32 flags,
                             const void *next)
{
    u32 status = rvx_next(next, reader, image, count, flags, 0, 0);
    void *db = RV_FIELD((void *)reader, ArtworkReader_database, void *);
    if (!status && active_db && db == active_db && (disabled || !share((void *)image)))
        last_kept = (void *)image;
    return status;
}

/* ArtworkDB photo albums name images by id while the database loads. */
static u32 album_image(u32 db, u32 id, const void *next)
{
    u32 image = rvx_next(next, db, id, 0, 0, 0, 0);
    for (u32 i = 0; !image && (void *)db == active_db && i < duplicate_count; i++)
        if (duplicates[i].id == id)
            image = (u32)duplicates[i].image;
    return image;
}

/* In place of the mhit parser's store of the image id: r0 = id, r4 = track. */
static u32 track_image(u32 id, u32 track, const void *next)
{
    (void)next;
    if (remapping) {
        struct duplicate *d;
        if (id) {
            if ((d = find(by_id, 0, id)))
                id = RV_FIELD(d->image, ArtworkImage_id, u32);
        } else if (find(by_dbid, 1, dbid_at((void *)track, TrackData_dbid))) {
            dbid_needed = 1;
        }
    }
    RV_FIELD((void *)track, TrackData_artwork_id, u32) = id;
    return 0;
}

/* After the iTunesDB tracks are parsed, ids are final. */
static u32 tracks_imported(u32 reader, const void *next)
{
    u32 status = rvx_next(next, reader, 0, 0, 0, 0, 0);
    remapping = 0;
    if (!dbid_needed)
        forget_duplicates();
    return status;
}

static u32 image_by_dbid(u32 db, u32 a1, u32 dbid_low, u32 dbid_high, const void *next,
                         u32 sorted)
{
    u32 image = rvx_next(next, db, a1, dbid_low, dbid_high, sorted, 0);
    if (!image && db && (void *)db == duplicate_db) {
        struct duplicate *d = find(by_dbid, 1, (uint64_t)dbid_high << 32 | dbid_low);
        if (d)
            image = (u32)d->image;
    }
    return image;
}

static const struct rvx_hook hooks[] = {
    {RV_SITE_ArtworkDatabase_Import_call, (u32)import, 0, RV_HOOK_MODE_FILTER, 0},
    {RV_SITE_ArtworkDatabase_ReadImageThumbs_call, (u32)image_thumbs_read, 0,
     RV_HOOK_MODE_FILTER, 0},
    {RV_SITE_ArtworkDatabase_FindById_PhotoAlbum_call, (u32)album_image, 0,
     RV_HOOK_MODE_FILTER, 0},
    {RV_SITE_TrackData_ArtworkId_word, (u32)track_image, 0, RV_HOOK_MODE_REPLACE, 0},
    {RV_SITE_ITunesDB_ImportTrackDataset_call, (u32)tracks_imported, 0,
     RV_HOOK_MODE_FILTER, 0},
    {RV_SITE_ArtworkDatabase_FindByDbid_0804581c_call, (u32)image_by_dbid, 0,
     RV_HOOK_MODE_FILTER, 0},
    {RV_SITE_ArtworkDatabase_FindByDbid_08046494_call, (u32)image_by_dbid, 0,
     RV_HOOK_MODE_FILTER, 0},
};

void rvx_artwork_bind(void)
{
    for (u32 i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        rvx_bind_core(&hooks[i]);
}
