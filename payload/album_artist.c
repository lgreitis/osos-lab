/* SPDX-License-Identifier: GPL-3.0-only */

#include "album_artist.h"
#include "patch.h"


struct artist_entry {
    uint32_t id_low, id_high, token;
    int used;
};

/* Share native strings; the side table owns one reference per retained field. */
static void *string_cache;
static struct artist_entry *entries;
static uint32_t capacity, count;
static void *owner;

static void release(uint32_t token)
{
    typedef int (*fn)(void *, uint32_t);
    if (token)
        ((fn)OSOS_IMPORT_STRING_CACHE_RELEASE)(string_cache, token);
}

static void clear(void)
{
    for (uint32_t i = 0; i < capacity; i++)
        release(entries[i].token);
    if (entries)
        ((void (*)(void *))OSOS_IMPORT_FREE)(entries);
    entries = NULL;
    capacity = count = 0;
    owner = string_cache = NULL;
}

static struct artist_entry *slot(struct artist_entry *table, uint32_t size,
                                 uint32_t low, uint32_t high)
{
    uint32_t hash = low ^ high;
    hash = (hash ^ (hash >> 16)) * 0x7feb352d;
    hash = (hash ^ (hash >> 15)) * 0x846ca68b;
    uint32_t index = (hash ^ (hash >> 16)) & (size - 1);
    while (table[index].used &&
           (table[index].id_low != low || table[index].id_high != high))
        index = (index + 1) & (size - 1);
    return &table[index];
}

static int grow(void)
{
    if (capacity && count < capacity * 3 / 4)
        return 1;
    if (capacity > UINT32_MAX / 2 / sizeof(*entries))
        return 0;
    uint32_t size = capacity ? capacity * 2 : 256;
    typedef void *(*allocate)(uint32_t);
    struct artist_entry *table = ((allocate)OSOS_IMPORT_ALLOC)(size * sizeof(*table));
    if (!table)
        return 0;
    for (uint32_t i = 0; i < size; i++) {
        table[i].token = 0;
        table[i].used = 0;
    }
    for (uint32_t i = 0; i < capacity; i++) {
        if (entries[i].used)
            *slot(table, size, entries[i].id_low, entries[i].id_high) = entries[i];
    }
    if (entries)
        ((void (*)(void *))OSOS_IMPORT_FREE)(entries);
    entries = table;
    capacity = size;
    return 1;
}

static const uint32_t *persistent_id(struct osos_media_record *record)
{
    return (const uint32_t *)((const uint8_t *)record + 0x110);
}

PATCH_ARM int cfw_album_artist_import_tracks(void *reader)
{
    clear();
    owner = *(void **)((uint8_t *)reader + OSOS_ITUNES_READER_OWNER_OFFSET);
    string_cache = (uint8_t *)owner + OSOS_TRACKDATA_ARTIST_CACHE_OFFSET;
    int status = ((int (*)(void *))OSOS_ITUNES_IMPORT_TRACKS)(reader);
    if (status)
        clear();
    return status;
}

PATCH_ARM void cfw_album_artist_destroy_tracks(void *track_data)
{
    if (track_data == owner)
        clear();
    ((void (*)(void *))OSOS_TRACKDATA_DESTROY_STORAGE)(track_data);
}

int cfw_album_artist_import(void *reader, struct osos_media_record *record,
                            uint32_t body_bytes)
{
    typedef int (*skip)(void *, int32_t);
    /* Native skip treats negative lengths as relative seeks. */
    if (body_bytes > INT32_MAX)
        return -0xd0;
    if (!string_cache || body_bytes < 16)
        return ((skip)OSOS_ITUNES_SKIP_BYTES)(reader, (int32_t)body_bytes);
    const uint32_t *id = persistent_id(record);
    struct artist_entry *entry = capacity ? slot(entries, capacity, id[0], id[1]) : NULL;
    if ((!entry || !entry->used) && !grow())
        return ((skip)OSOS_ITUNES_SKIP_BYTES)(reader, (int32_t)body_bytes);

    void *file = *(void **)((uint8_t *)reader + OSOS_ITUNES_READER_FILE_OFFSET);
    typedef int (*position)(void *, uint64_t *);
    uint64_t start, end;
    if (((position)OSOS_IMPORT_FILE_POSITION)(file, &start))
        return ((skip)OSOS_ITUNES_SKIP_BYTES)(reader, (int32_t)body_bytes);

    typedef int (*import)(void *, uint32_t, uint32_t, void *, uint32_t *, uint32_t, int);
    uint32_t token = 0;
    int status = ((import)OSOS_ITUNES_IMPORT_STRING)(
        reader, 1, 22, string_cache, &token, 0, 1);
    int cursor_status = ((position)OSOS_IMPORT_FILE_POSITION)(file, &end);
    if (cursor_status || end < start || end - start > body_bytes) {
        release(token);
        return cursor_status ? cursor_status : -0xd0;
    }
    /* Consume any retained padding, including after an optional-field decode failure. */
    int trailing_status = ((skip)OSOS_ITUNES_SKIP_BYTES)(
        reader, (int32_t)(body_bytes - (uint32_t)(end - start)));
    if (trailing_status) {
        release(token);
        return trailing_status;
    }
    if (status) {
        release(token);
        token = 0;
    }

    entry = slot(entries, capacity, id[0], id[1]);
    release(entry->token);
    if (!entry->used)
        count++;
    *entry = (struct artist_entry){id[0], id[1], token, 1};
    return 0;
}

uint32_t cfw_album_artist_token(struct osos_media_record *record)
{
    if (!record || !capacity || *(void **)record != owner)
        return 0;
    const uint32_t *id = persistent_id(record);
    return slot(entries, capacity, id[0], id[1])->token;
}

uint32_t cfw_album_artist_read(struct osos_media_record *record,
                                uint16_t *text, uint32_t max_units)
{
    uint32_t token = cfw_album_artist_token(record);
    if (!token || !max_units || max_units > INT32_MAX / 2)
        return 0;
    uint32_t bytes = 0;
    typedef int (*read)(void *, uint32_t, void *, uint32_t *, uint32_t);
    if (((read)OSOS_IMPORT_STRING_CACHE_READ)(
            string_cache, token, text, &bytes, max_units * 2))
        return 0;
    uint32_t units = bytes / 2;
    if (units && text[units - 1] >= 0xd800 && text[units - 1] <= 0xdbff)
        units--;
    return units;
}

