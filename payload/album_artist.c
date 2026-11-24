/* SPDX-License-Identifier: GPL-3.0-only */

#include "album_artist.h"
#include "album_artists.h"
#include "patch.h"

enum {
    ID_LOW,
    ID_HIGH,
    ALBUM_ARTIST,
    SORT_ALBUM_ARTIST,
    SORT_ARTIST,
    COLUMN_COUNT,
    BUCKET_COUNT = 256,
    MHOD_SORT_ARTIST = 23,
    MHOD_SORT_ALBUM_ARTIST = 29,
};

struct artist_row {
    uint32_t values[COLUMN_COUNT];
};

/* Each column stores only the low bytes that vary within this bucket. */
struct packed_rows {
    uint32_t base[COLUMN_COUNT];
    uint8_t offsets[COLUMN_COUNT + 1];
    uint8_t data[];
};

struct bucket {
    union {
        struct artist_row *rows;
        struct packed_rows *packed;
    };

    uint32_t count, capacity; /* Nonempty buckets with zero capacity are packed. */
};

static struct bucket *buckets;
static void *owner, *string_cache;
static int finalized;
static uint32_t last_index;

static void release(uint32_t token)
{
    if (token)
        osos_import_string_cache_release(string_cache, token);
}

static uint32_t value_at(const struct bucket *bucket, uint32_t index, unsigned column)
{
    if (bucket->capacity)
        return bucket->rows[index].values[column];
    const struct packed_rows *packed = bucket->packed;
    const uint8_t *row = packed->data + index * packed->offsets[COLUMN_COUNT];
    unsigned begin = packed->offsets[column], end = packed->offsets[column + 1];
    uint32_t value = packed->base[column];
    for (unsigned i = begin; i < end; i++)
        value ^= (uint32_t)row[i] << (8 * (i - begin));
    return value;
}

static void clear(void)
{
    if (owner)
        cfw_album_artists_invalidate(owner);
    if (buckets) {
        for (unsigned i = 0; i < BUCKET_COUNT; i++) {
            struct bucket *b = &buckets[i];
            for (uint32_t row = 0; row < b->count; row++)
                for (unsigned col = ALBUM_ARTIST; col < COLUMN_COUNT; col++)
                    release(value_at(b, row, col));
            if (b->capacity)
                osos_import_free(b->rows);
            else if (b->count)
                osos_import_free(b->packed);
        }
        osos_import_free(buckets);
        buckets = NULL;
    }
    owner = string_cache = NULL;
    finalized = 0;
    last_index = 0;
}

static const uint32_t *persistent_id(struct osos_media_record *record)
{
    return (const uint32_t *)((const uint8_t *)record + 0x110);
}

static struct bucket *bucket_for(const uint32_t *id)
{
    uint32_t hash = id[0] ^ (id[1] * 0x9e3779b9u);
    hash = (hash ^ (hash >> 16)) * 0x7feb352d;
    return &buckets[hash >> 24];
}

static uint32_t find_row(const struct bucket *b, const uint32_t *id, int *found)
{
    uint32_t low = 0, high = b->count;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2;
        uint32_t hi = value_at(b, mid, ID_HIGH);
        if (hi < id[1] || (hi == id[1] && value_at(b, mid, ID_LOW) < id[0]))
            low = mid + 1;
        else
            high = mid;
    }
    *found = low < b->count && value_at(b, low, ID_HIGH) == id[1] &&
             value_at(b, low, ID_LOW) == id[0];
    return low;
}

static struct artist_row *insert_row(struct bucket *b, uint32_t at, const uint32_t *id)
{
    if (b->count == b->capacity) {
        uint32_t extra = b->capacity < 32 ? (b->capacity ? b->capacity : 4) : 32;
        if (b->capacity > UINT32_MAX / sizeof(*b->rows) - extra)
            return NULL;
        struct artist_row *rows =
            osos_import_alloc((b->capacity + extra) * sizeof(*rows));
        if (!rows)
            return NULL;
        for (uint32_t i = 0; i < b->count; i++)
            rows[i + (i >= at)] = b->rows[i];
        if (b->rows)
            osos_import_free(b->rows);
        b->rows = rows;
        b->capacity += extra;
    } else {
        for (uint32_t i = b->count; i > at; i--)
            b->rows[i] = b->rows[i - 1];
    }
    b->count++;
    b->rows[at] = (struct artist_row){{id[0], id[1], 0, 0, 0}};
    return &b->rows[at];
}

static void pack_bucket(struct bucket *b)
{
    uint32_t used = 0, varying[COLUMN_COUNT];
    for (unsigned c = 0; c < COLUMN_COUNT; c++)
        varying[c] = 0;
    for (uint32_t j = 0; j < b->count; j++) {
        struct artist_row *row = &b->rows[j];
        if (!(row->values[ALBUM_ARTIST] | row->values[SORT_ALBUM_ARTIST] |
              row->values[SORT_ARTIST]))
            continue;
        b->rows[used++] = *row;
        for (unsigned c = 0; c < COLUMN_COUNT; c++)
            varying[c] |= row->values[c] ^ b->rows[0].values[c];
    }
    b->count = used;
    if (!used) {
        osos_import_free(b->rows);
        b->rows = NULL;
        b->capacity = 0;
        return;
    }
    uint8_t offsets[COLUMN_COUNT + 1] = {0};
    for (unsigned c = 0; c < COLUMN_COUNT; c++) {
        offsets[c + 1] = offsets[c];
        for (uint32_t v = varying[c]; v; v >>= 8)
            offsets[c + 1]++;
    }
    uint32_t stride = offsets[COLUMN_COUNT];
    uint32_t available = b->capacity * sizeof(*b->rows);
    if (available < sizeof(struct packed_rows) ||
        (stride && used > (available - sizeof(struct packed_rows)) / stride))
        return;
    struct packed_rows *packed = osos_import_alloc(sizeof(*packed) + used * stride);
    if (!packed)
        return; /* The sorted import rows remain usable under memory pressure. */
    for (unsigned c = 0; c < COLUMN_COUNT; c++)
        packed->base[c] = b->rows[0].values[c];
    for (unsigned c = 0; c <= COLUMN_COUNT; c++)
        packed->offsets[c] = offsets[c];
    uint8_t *out = packed->data;
    for (uint32_t j = 0; j < used; j++)
        for (unsigned c = 0; c < COLUMN_COUNT; c++) {
            uint32_t delta = b->rows[j].values[c] ^ packed->base[c];
            for (unsigned k = offsets[c]; k < offsets[c + 1]; k++, delta >>= 8)
                *out++ = (uint8_t)delta;
        }
    osos_import_free(b->rows);
    b->packed = packed;
    b->capacity = 0;
}

/* Pack and release each bucket before allocating the next one. */
static void compact(void)
{
    if (!buckets)
        return;
    for (unsigned i = 0; i < BUCKET_COUNT; i++)
        if (buckets[i].capacity)
            pack_bucket(&buckets[i]);
    finalized = 1;
}

static uint32_t record_token(struct osos_media_record *record, unsigned column)
{
    if (!record || !buckets || *(void **)record != owner)
        return 0;
    const uint32_t *id = persistent_id(record);
    struct bucket *b = bucket_for(id);
    uint32_t cached = last_index;
    if (cached < b->count && value_at(b, cached, ID_HIGH) == id[1] &&
        value_at(b, cached, ID_LOW) == id[0])
        return value_at(b, cached, column);
    int found;
    uint32_t at = find_row(b, id, &found);
    if (!found)
        return 0;
    last_index = at;
    return value_at(b, at, column);
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
    if (!string_cache || finalized || body_bytes < 16)
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

    if (!buckets && token) {
        buckets = osos_import_alloc(BUCKET_COUNT * sizeof(*buckets));
        if (buckets)
            for (unsigned i = 0; i < BUCKET_COUNT; i++) {
                buckets[i].rows = NULL;
                buckets[i].count = buckets[i].capacity = 0;
            }
    }
    if (!buckets) {
        release(token);
        return 0;
    }
    struct bucket *b = bucket_for(id);
    int found;
    uint32_t at = find_row(b, id, &found);
    if (!found && !token) {
        release(token);
        return 0;
    }
    struct artist_row *row = found ? &b->rows[at] : insert_row(b, at, id);
    if (!row) {
        release(token);
        return 0;
    }
    unsigned col = mhod_type == MHOD_SORT_ALBUM_ARTIST ? SORT_ALBUM_ARTIST
                   : mhod_type == MHOD_SORT_ARTIST     ? SORT_ARTIST
                                                       : ALBUM_ARTIST;
    release(row->values[col]);
    row->values[col] = token;
    return 0;
}

uint32_t cfw_album_artist_token(struct osos_media_record *record)
{
    return record_token(record, ALBUM_ARTIST);
}

uint32_t cfw_album_artist_sort_token(struct osos_media_record *record, int album_artist)
{
    return record_token(record, album_artist ? SORT_ALBUM_ARTIST : SORT_ARTIST);
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
