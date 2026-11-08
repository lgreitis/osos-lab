/* SPDX-License-Identifier: GPL-3.0-only */

#include "album_artist.h"
#include "album_artists.h"
#include "artwork_thumbs.h"
#include "patch.h"

/* Import-time and post-import updates; record is only used to compact the import. */
struct artist_entry {
    uint32_t id_low, id_high, token, sort_album_artist, sort_artist;
    struct osos_media_record *record;
    int used; /* ENTRY_USED plus the *_BORROWED fields, which are never released */
};

/* Compacted import, sorted by ID. */
struct artist_fields {
    uint32_t id_low, id_high, token, sort_album_artist, sort_artist;
};

/* Songs whose Album Artist token is their Artist token and that have no Sort Album
 * Artist. SAME_SORTED songs also use that token as Sort Artist; SAME_PLAIN have none. */
struct artist_id {
    uint32_t id_low, id_high;
};

enum {
    ENTRY_USED = 1,
    ENTRY_TOKEN_BORROWED = 2,
    ENTRY_SORT_ARTIST_BORROWED = 4,
    SAME_PLAIN = 0,
    SAME_SORTED = 1,
    SAME_KINDS = 2,
    MHOD_SORT_ARTIST = 23,
    MHOD_SORT_ALBUM_ARTIST = 29,
    RECORD_ARTIST_TOKEN = 0x2c,
    RECORD_PERSISTENT_ID = 0x110,
};

/* Share native strings; the side table owns one reference per retained field. The
 * record's Artist token holds no reference of its own, so same-as-Artist songs keep
 * theirs without storing the token, and the native cache frees it with the library. */
static void *string_cache;
static struct artist_entry *entries;
static uint32_t capacity, count;
static struct artist_fields *fields;
static struct artist_id *same[SAME_KINDS];
static uint32_t field_count, same_count[SAME_KINDS];
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
        if (!(entries[i].used & ENTRY_TOKEN_BORROWED))
            release(entries[i].token);
        release(entries[i].sort_album_artist);
        if (!(entries[i].used & ENTRY_SORT_ARTIST_BORROWED))
            release(entries[i].sort_artist);
    }
    for (uint32_t i = 0; i < field_count; i++) {
        release(fields[i].token);
        release(fields[i].sort_album_artist);
        release(fields[i].sort_artist);
    }
    if (entries)
        osos_import_free(entries);
    if (fields)
        osos_import_free(fields);
    for (int kind = 0; kind < SAME_KINDS; kind++) {
        if (same[kind])
            osos_import_free(same[kind]);
        same[kind] = NULL;
        same_count[kind] = 0;
    }
    entries = NULL;
    fields = NULL;
    capacity = count = field_count = 0;
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
        table[i].record = NULL;
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
    return (const uint32_t *)((const uint8_t *)record + RECORD_PERSISTENT_ID);
}

static uint32_t artist_token(struct osos_media_record *record)
{
    return *(const uint32_t *)((const uint8_t *)record + RECORD_ARTIST_TOKEN);
}

static int id_before(uint32_t low, uint32_t high, uint32_t other_low,
                     uint32_t other_high)
{
    return high != other_high ? high < other_high : low < other_low;
}

/* Lower bound over a sorted array whose elements start with id_low, id_high. */
static uint32_t search(const void *base, uint32_t size, uint32_t elements, uint32_t low,
                       uint32_t high)
{
    uint32_t first = 0;
    while (elements) {
        uint32_t half = elements / 2;
        const uint32_t *id =
            (const uint32_t *)((const uint8_t *)base + (first + half) * size);
        if (id_before(id[0], id[1], low, high)) {
            first += half + 1;
            elements -= half + 1;
        } else {
            elements = half;
        }
    }
    return first;
}

static void swap(uint8_t *a, uint8_t *b, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        uint8_t byte = a[i];
        a[i] = b[i];
        b[i] = byte;
    }
}

static int element_before(const uint8_t *a, const uint8_t *b)
{
    const uint32_t *left = (const uint32_t *)a, *right = (const uint32_t *)b;
    return id_before(left[0], left[1], right[0], right[1]);
}

static void sift(uint8_t *base, uint32_t size, uint32_t root, uint32_t elements)
{
    for (;;) {
        uint32_t child = root * 2 + 1;
        if (child >= elements)
            return;
        if (child + 1 < elements &&
            element_before(base + child * size, base + (child + 1) * size))
            child++;
        if (!element_before(base + root * size, base + child * size))
            return;
        swap(base + root * size, base + child * size, size);
        root = child;
    }
}

/* Heapsort: in place, so compaction needs no scratch allocation. */
static void sort_ids(void *array, uint32_t size, uint32_t elements)
{
    uint8_t *base = array;
    for (uint32_t i = elements / 2; i-- > 0;)
        sift(base, size, i, elements);
    for (uint32_t end = elements; end > 1; end--) {
        swap(base, base + (end - 1) * size, size);
        sift(base, size, 0, end - 1);
    }
}

/* The compact kind of a song, or -1 when it needs its own fields. */
static int same_kind(const struct artist_entry *entry)
{
    if (!entry->token || !entry->record ||
        entry->token != artist_token(entry->record) || entry->sort_album_artist)
        return -1;
    if (!entry->sort_artist)
        return SAME_PLAIN;
    return entry->sort_artist == entry->token ? SAME_SORTED : -1;
}

static int has_fields(const struct artist_entry *entry)
{
    return entry->token || entry->sort_album_artist || entry->sort_artist;
}

/* Replace the import table with exact-size sorted arrays. A failed allocation keeps
 * the table, which answers the same lookups. */
static void compact(void)
{
    uint32_t full = 0, kinds[SAME_KINDS] = {0};
    for (uint32_t i = 0; i < capacity; i++) {
        const struct artist_entry *entry = &entries[i];
        if (!entry->used)
            continue;
        int kind = same_kind(entry);
        if (kind >= 0)
            kinds[kind]++;
        else if (has_fields(entry))
            full++;
    }
    struct artist_fields *full_array =
        full ? osos_import_alloc(full * sizeof(*full_array)) : NULL;
    struct artist_id *same_arrays[SAME_KINDS];
    int failed = full && !full_array;
    for (int kind = 0; kind < SAME_KINDS; kind++) {
        same_arrays[kind] =
            kinds[kind] ? osos_import_alloc(kinds[kind] * sizeof(**same_arrays)) : NULL;
        failed |= kinds[kind] && !same_arrays[kind];
    }
    if (failed) {
        if (full_array)
            osos_import_free(full_array);
        for (int kind = 0; kind < SAME_KINDS; kind++)
            if (same_arrays[kind])
                osos_import_free(same_arrays[kind]);
        return;
    }
    uint32_t f = 0, s[SAME_KINDS] = {0};
    for (uint32_t i = 0; i < capacity; i++) {
        const struct artist_entry *entry = &entries[i];
        if (!entry->used)
            continue;
        int kind = same_kind(entry);
        if (kind >= 0) {
            same_arrays[kind][s[kind]++] =
                (struct artist_id){entry->id_low, entry->id_high};
        } else if (has_fields(entry)) {
            full_array[f++] =
                (struct artist_fields){entry->id_low, entry->id_high, entry->token,
                                       entry->sort_album_artist, entry->sort_artist};
        }
    }
    osos_import_free(entries);
    entries = NULL;
    capacity = count = 0;
    sort_ids(full_array, sizeof(*full_array), full);
    fields = full_array;
    field_count = full;
    for (int kind = 0; kind < SAME_KINDS; kind++) {
        sort_ids(same_arrays[kind], sizeof(**same_arrays), kinds[kind]);
        same[kind] = same_arrays[kind];
        same_count[kind] = kinds[kind];
    }
}

static struct artist_fields *find_fields(uint32_t low, uint32_t high)
{
    uint32_t i = search(fields, sizeof(*fields), field_count, low, high);
    return i < field_count && fields[i].id_low == low && fields[i].id_high == high
               ? &fields[i]
               : NULL;
}

/* The compact kind holding a song, or -1. */
static int find_same(uint32_t low, uint32_t high)
{
    for (int kind = 0; kind < SAME_KINDS; kind++) {
        const struct artist_id *ids = same[kind];
        uint32_t i = search(ids, sizeof(*ids), same_count[kind], low, high);
        if (i < same_count[kind] && ids[i].id_low == low && ids[i].id_high == high)
            return kind;
    }
    return -1;
}

/* Copy a record's fields; zero means absent. */
static int lookup(struct osos_media_record *record, struct artist_fields *out)
{
    if (!record || *(void **)record != owner || !owner)
        return 0;
    const uint32_t *id = persistent_id(record);
    if (capacity) {
        struct artist_entry *entry = slot(entries, capacity, id[0], id[1]);
        if (entry->used) {
            *out = (struct artist_fields){id[0], id[1], entry->token,
                                          entry->sort_album_artist, entry->sort_artist};
            return 1;
        }
    }
    struct artist_fields *compact_fields = find_fields(id[0], id[1]);
    if (compact_fields) {
        *out = *compact_fields;
        return 1;
    }
    int kind = find_same(id[0], id[1]);
    if (kind >= 0) {
        uint32_t token = artist_token(record);
        *out = (struct artist_fields){id[0], id[1], token, 0,
                                      kind == SAME_SORTED ? token : 0};
        return 1;
    }
    return 0;
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

/* Move a compacted song into the update table, which then owns its references. */
static void take_compacted(struct artist_entry *entry, struct osos_media_record *record,
                           uint32_t low, uint32_t high)
{
    struct artist_fields *compact_fields = find_fields(low, high);
    if (compact_fields) {
        entry->token = compact_fields->token;
        entry->sort_album_artist = compact_fields->sort_album_artist;
        entry->sort_artist = compact_fields->sort_artist;
        compact_fields->token = compact_fields->sort_album_artist =
            compact_fields->sort_artist = 0;
        return;
    }
    int kind = find_same(low, high);
    if (kind < 0)
        return;
    entry->token = artist_token(record);
    entry->used |= ENTRY_TOKEN_BORROWED;
    if (kind == SAME_SORTED) {
        entry->sort_artist = entry->token;
        entry->used |= ENTRY_SORT_ARTIST_BORROWED;
    }
}

PATCH_ARM int cfw_album_artist_import_tracks(void *reader)
{
    clear();
    owner = *(void **)((uint8_t *)reader + OSOS_ITUNES_READER_OWNER_OFFSET);
    string_cache = (uint8_t *)owner + OSOS_TRACKDATA_ARTIST_CACHE_OFFSET;
    int status = osos_itunes_import_tracks(reader);
    cfw_artwork_tracks_imported();
    if (status)
        clear();
    else
        compact();
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
        entry->used = ENTRY_USED;
        entry->record = record;
        take_compacted(entry, record, id[0], id[1]);
    }
    uint32_t *field = metadata_field(entry, mhod_type);
    int borrowed = field == &entry->token         ? ENTRY_TOKEN_BORROWED
                   : field == &entry->sort_artist ? ENTRY_SORT_ARTIST_BORROWED
                                                  : 0;
    if (entry->used & borrowed)
        entry->used &= ~borrowed;
    else
        release(*field);
    *field = token;
    return 0;
}

uint32_t cfw_album_artist_token(struct osos_media_record *record)
{
    struct artist_fields found;
    return lookup(record, &found) ? found.token : 0;
}

uint32_t cfw_album_artist_sort_token(struct osos_media_record *record, int album_artist)
{
    struct artist_fields found;
    if (!lookup(record, &found))
        return 0;
    return album_artist ? found.sort_album_artist : found.sort_artist;
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
