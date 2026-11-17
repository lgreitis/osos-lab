/* SPDX-License-Identifier: GPL-3.0-only */

#include "album_artist.h"
#include "album_artists.h"
#include "patch.h"

struct artist_entry {
    uint32_t id_low, id_high, tokens[3];
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
/* Import uses a hash table; playback uses sorted IDs and only populated columns. */
static uint32_t *packed;
static uint32_t packed_stride, packed_offsets[3];
static const uint32_t *last_row;
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
    last_row = NULL;
    for (uint32_t i = 0; i < capacity; i++)
        for (unsigned field = 0; field < 3; field++)
            release(entries[i].tokens[field]);
    if (packed) {
        for (uint32_t i = 0; i < count; i++)
            for (uint32_t field = 2; field < packed_stride; field++)
                release(packed[i * packed_stride + field]);
        osos_import_free(packed);
    }
    if (entries)
        osos_import_free(entries);
    entries = NULL;
    packed = NULL;
    packed_stride = 0;
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
        for (unsigned field = 0; field < 3; field++)
            table[i].tokens[field] = 0;
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

static int compare_ids(const uint32_t *a, const uint32_t *b)
{
    if (a[1] != b[1])
        return a[1] > b[1] ? 1 : -1;
    return (a[0] > b[0]) - (a[0] < b[0]);
}

static void swap_rows(uint32_t *a, uint32_t *b, uint32_t stride)
{
    for (uint32_t i = 0; i < stride; i++) {
        uint32_t value = a[i];
        a[i] = b[i];
        b[i] = value;
    }
}

static void sift_rows(uint32_t *rows, uint32_t stride, uint32_t root, uint32_t size)
{
    while (root < size / 2) {
        uint32_t child = root * 2 + 1;
        if (child + 1 < size &&
            compare_ids(rows + child * stride, rows + (child + 1) * stride) < 0)
            child++;
        if (compare_ids(rows + root * stride, rows + child * stride) >= 0)
            break;
        swap_rows(rows + root * stride, rows + child * stride, stride);
        root = child;
    }
}

static void compact(void)
{
    uint32_t used = 0, fields = 0;
    for (uint32_t i = 0; i < capacity; i++) {
        uint32_t present = 0;
        for (unsigned field = 0; field < 3; field++)
            if (entries[i].tokens[field])
                present |= 1u << field;
        used += present != 0;
        fields |= present;
    }
    uint32_t stride = 2;
    for (unsigned field = 0; field < 3; field++)
        packed_offsets[field] = fields & (1u << field) ? stride++ : 0;
    uint32_t *rows = used ? osos_import_alloc(used * stride * sizeof(*rows)) : NULL;
    if (used && !rows)
        return; /* Keep the working hash table if compaction cannot allocate. */
    uint32_t next = 0;
    for (uint32_t i = 0; i < capacity; i++) {
        const struct artist_entry *entry = &entries[i];
        if (!entry->tokens[0] && !entry->tokens[1] && !entry->tokens[2])
            continue;
        uint32_t *row = rows + next++ * stride;
        row[0] = entry->id_low;
        row[1] = entry->id_high;
        for (unsigned field = 0; field < 3; field++)
            if (packed_offsets[field])
                row[packed_offsets[field]] = entry->tokens[field];
    }
    for (uint32_t root = used / 2; root; root--)
        sift_rows(rows, stride, root - 1, used);
    for (uint32_t end = used; end > 1; end--) {
        swap_rows(rows, rows + (end - 1) * stride, stride);
        sift_rows(rows, stride, 0, end - 1);
    }
    if (entries)
        osos_import_free(entries);
    entries = NULL;
    capacity = 0;
    count = used;
    packed = rows;
    packed_stride = stride;
}

static uint32_t record_token(struct osos_media_record *record, unsigned field)
{
    if (!record || *(void **)record != owner)
        return 0;
    const uint32_t *id = persistent_id(record);
    if (packed) {
        if (!packed_offsets[field])
            return 0;
        if (last_row && !compare_ids(id, last_row))
            return last_row[packed_offsets[field]];
        uint32_t low = 0, high = count;
        while (low < high) {
            uint32_t middle = low + (high - low) / 2;
            const uint32_t *row = packed + middle * packed_stride;
            int order = compare_ids(id, row);
            if (!order) {
                last_row = row;
                return row[packed_offsets[field]];
            }
            if (order < 0)
                high = middle;
            else
                low = middle + 1;
        }
        return 0;
    }
    if (!capacity)
        return 0;
    struct artist_entry *entry = slot(entries, capacity, id[0], id[1]);
    return entry->used ? entry->tokens[field] : 0;
}

PATCH_ARM int cfw_album_artist_import_tracks(void *reader)
{
    clear();
    owner = *(void **)((uint8_t *)reader + OSOS_ITUNES_READER_OWNER_OFFSET);
    string_cache = (uint8_t *)owner + OSOS_TRACKDATA_ARTIST_CACHE_OFFSET;
    int status = osos_itunes_import_tracks(reader);
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
    if (!string_cache || packed || body_bytes < 16)
        return osos_itunes_skip_bytes(reader, (int32_t)body_bytes);
    const uint32_t *id = persistent_id(record);

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

    struct artist_entry *entry =
        capacity ? slot(entries, capacity, id[0], id[1]) : NULL;
    if (!entry || !entry->used) {
        if (!token)
            return 0;
        if (!grow()) {
            release(token);
            return 0;
        }
        entry = slot(entries, capacity, id[0], id[1]);
        count++;
        entry->id_low = id[0];
        entry->id_high = id[1];
        entry->used = 1;
    }
    unsigned index = mhod_type == MHOD_SORT_ALBUM_ARTIST ? 1
                     : mhod_type == MHOD_SORT_ARTIST     ? 2
                                                         : 0;
    uint32_t *field = &entry->tokens[index];
    release(*field);
    *field = token;
    return 0;
}

uint32_t cfw_album_artist_token(struct osos_media_record *record)
{
    return record_token(record, 0);
}

uint32_t cfw_album_artist_sort_token(struct osos_media_record *record, int album_artist)
{
    return record_token(record, album_artist ? 1 : 2);
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
