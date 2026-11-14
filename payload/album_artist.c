/* SPDX-License-Identifier: GPL-3.0-only */

#include "album_artist.h"
#include "album_artists.h"
#include "patch.h"

struct artist_entry {
    uint32_t id_low, id_high, token, sort_album_artist, sort_artist;
    int used;
};

enum {
    MHOD_SORT_ARTIST = 23,
    MHOD_SORT_ALBUM_ARTIST = 29,
};

/* Share native strings; the side table owns one reference per retained field. */
static void *string_cache;
static struct artist_entry *entries;
static uint32_t capacity, count;
static void *owner;

static void release(uint32_t token)
{
    if (token)
        osos_import_string_cache_release(string_cache, token);
}

static void clear(void)
{
    if (owner)
        cfw_album_artists_invalidate(owner);
    for (uint32_t i = 0; i < capacity; i++) {
        release(entries[i].token);
        release(entries[i].sort_album_artist);
        release(entries[i].sort_artist);
    }
    if (entries)
        osos_import_free(entries);
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
    struct artist_entry *table = osos_import_alloc(size * sizeof(*table));
    if (!table)
        return 0;
    for (uint32_t i = 0; i < size; i++) {
        table[i].token = table[i].sort_album_artist = table[i].sort_artist = 0;
        table[i].used = 0;
    }
    for (uint32_t i = 0; i < capacity; i++) {
        if (entries[i].used)
            *slot(table, size, entries[i].id_low, entries[i].id_high) = entries[i];
    }
    if (entries)
        osos_import_free(entries);
    entries = table;
    capacity = size;
    return 1;
}

static const uint32_t *persistent_id(struct osos_media_record *record)
{
    return (const uint32_t *)((const uint8_t *)record + 0x110);
}

static struct artist_entry *find_record(struct osos_media_record *record)
{
    if (!record || !capacity || *(void **)record != owner)
        return NULL;
    const uint32_t *id = persistent_id(record);
    struct artist_entry *entry = slot(entries, capacity, id[0], id[1]);
    return entry->used ? entry : NULL;
}

static uint32_t *metadata_field(struct artist_entry *entry, uint32_t mhod_type)
{
    switch (mhod_type) {
    case MHOD_SORT_ALBUM_ARTIST:
        return &entry->sort_album_artist;
    case MHOD_SORT_ARTIST:
        return &entry->sort_artist;
    default:
        return &entry->token;
    }
}

PATCH_ARM int cfw_album_artist_import_tracks(void *reader)
{
    clear();
    owner = *(void **)((uint8_t *)reader + OSOS_ITUNES_READER_OWNER_OFFSET);
    string_cache = (uint8_t *)owner + OSOS_TRACKDATA_ARTIST_CACHE_OFFSET;
    int status = osos_itunes_import_tracks(reader);
    if (status)
        clear();
    return status;
}

PATCH_ARM void cfw_album_artist_destroy_tracks(void *track_data)
{
    if (track_data == owner)
        clear();
    osos_trackdata_destroy_storage(track_data);
}

int cfw_album_artist_import(void *reader, struct osos_media_record *record,
                            uint32_t body_bytes, uint32_t mhod_type)
{
    /* Native skip treats negative lengths as relative seeks. */
    if (body_bytes > INT32_MAX)
        return -0xd0;
    if (!string_cache || body_bytes < 16)
        return osos_itunes_skip_bytes(reader, (int32_t)body_bytes);
    const uint32_t *id = persistent_id(record);
    struct artist_entry *entry =
        capacity ? slot(entries, capacity, id[0], id[1]) : NULL;
    if ((!entry || !entry->used) && !grow())
        return osos_itunes_skip_bytes(reader, (int32_t)body_bytes);

    void *file = *(void **)((uint8_t *)reader + OSOS_ITUNES_READER_FILE_OFFSET);
    uint64_t start, end;
    if (osos_import_file_position(file, &start))
        return osos_itunes_skip_bytes(reader, (int32_t)body_bytes);

    uint32_t token = 0;
    int status =
        osos_itunes_import_string(reader, 1, mhod_type, string_cache, &token, 0, 1);
    int cursor_status = osos_import_file_position(file, &end);
    if (cursor_status || end < start || end - start > body_bytes) {
        release(token);
        return cursor_status ? cursor_status : -0xd0;
    }
    /* Consume any retained padding, including after an optional-field decode failure. */
    int trailing_status =
        osos_itunes_skip_bytes(reader, (int32_t)(body_bytes - (uint32_t)(end - start)));
    if (trailing_status) {
        release(token);
        return trailing_status;
    }
    if (status) {
        release(token);
        token = 0;
    }

    entry = slot(entries, capacity, id[0], id[1]);
    if (!entry->used) {
        count++;
        entry->id_low = id[0];
        entry->id_high = id[1];
        entry->used = 1;
    }
    uint32_t *field = metadata_field(entry, mhod_type);
    release(*field);
    *field = token;
    return 0;
}

uint32_t cfw_album_artist_token(struct osos_media_record *record)
{
    struct artist_entry *entry = find_record(record);
    return entry ? entry->token : 0;
}

uint32_t cfw_album_artist_sort_token(struct osos_media_record *record, int album_artist)
{
    struct artist_entry *entry = find_record(record);
    if (!entry)
        return 0;
    return album_artist ? entry->sort_album_artist : entry->sort_artist;
}

uint32_t cfw_album_artist_read(struct osos_media_record *record, uint16_t *text,
                               uint32_t max_units)
{
    uint32_t token = cfw_album_artist_token(record);
    if (!token || !max_units || max_units > INT32_MAX / 2)
        return 0;
    uint32_t bytes = 0;
    if (osos_import_string_cache_read(string_cache, token, text, &bytes, max_units * 2))
        return 0;
    uint32_t units = bytes / 2;
    if (units && text[units - 1] >= 0xd800 && text[units - 1] <= 0xdbff)
        units--;
    return units;
}
