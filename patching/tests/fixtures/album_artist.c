/* SPDX-License-Identifier: GPL-3.0-only */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM

struct osos_media_record {
    union {
        void *owner;
        uint8_t bytes[0x118];
    } data;
};

struct osos_media_item {
    struct osos_media_record *record;
};

struct file {
    uint64_t offset;
    const uint16_t *text;
    uint32_t units;
    int decode_error, skip_error, position_error;
};

struct reader {
    struct file *file;
    void *owner;
};

static void mock_destroy(void *);
static int mock_release(void *, uint32_t);
static int mock_read(void *, uint32_t, void *, uint32_t *, uint32_t);
static int mock_decode(void *, uint32_t, uint32_t, void *, uint32_t *, uint32_t, int);
static int mock_tracks(void *);
static int mock_skip(void *, int32_t);
static int mock_position(void *, uint64_t *);
static void *mock_alloc(uint32_t);
static void mock_free(void *);
#define osos_trackdata_destroy_storage mock_destroy
#define OSOS_TRACKDATA_ARTIST_CACHE_OFFSET 0x118
#define osos_import_string_cache_release mock_release
#define osos_import_string_cache_read mock_read
#define osos_itunes_import_string mock_decode
#define osos_itunes_import_tracks mock_tracks
#define osos_itunes_skip_bytes mock_skip
#define osos_import_file_position mock_position
#define osos_import_alloc mock_alloc
#define osos_import_free mock_free
#define OSOS_ITUNES_READER_FILE_OFFSET offsetof(struct reader, file)
#define OSOS_ITUNES_READER_OWNER_OFFSET offsetof(struct reader, owner)
#include "../../../payload/album_artist.c"

void cfw_album_artists_invalidate(void *track_data)
{
    assert(track_data == owner);
}

struct cached {
    uint16_t *text;
    uint32_t bytes, refs;
};
static struct cached tokens[4096];
static uint32_t next_token = 1, live_refs;
static size_t live_bytes, peak_bytes;

union allocation {
    max_align_t alignment;
    size_t bytes;
};

static int fail_alloc, track_status, frozen;
static void (*during_import)(struct reader *);

static int mock_release(void *cache, uint32_t token)
{
    assert(cache == string_cache && tokens[token].refs);
    if (frozen)
        return -50;
    live_refs--;
    if (!--tokens[token].refs) {
        free(tokens[token].text);
        tokens[token].text = NULL;
    }
    return 0;
}

static int mock_read(void *cache, uint32_t token, void *out, uint32_t *bytes,
                     uint32_t limit)
{
    assert(cache == string_cache && tokens[token].refs);
    *bytes = tokens[token].bytes < limit ? tokens[token].bytes : limit;
    memcpy(out, tokens[token].text, *bytes);
    return 0;
}

static int mock_decode(void *context, uint32_t encoding, uint32_t type, void *cache,
                       uint32_t *token, uint32_t glyph_field, int collect)
{
    struct file *file = ((struct reader *)context)->file;
    assert(encoding == 1 && (type == 22 || type == 23 || type == 29) &&
           cache == string_cache && glyph_field == 0 && collect == 1);
    if (file->decode_error) {
        file->offset += 8;
        return file->decode_error;
    }
    file->offset += 16 + file->units * 2;
    *token = 0;
    if (!file->units)
        return 0;
    for (uint32_t i = 1; i < next_token; i++) {
        if (tokens[i].refs && tokens[i].bytes == file->units * 2 &&
            !memcmp(tokens[i].text, file->text, tokens[i].bytes)) {
            *token = i;
            tokens[i].refs++;
            live_refs++;
            return 0;
        }
    }
    assert(next_token < sizeof(tokens) / sizeof(tokens[0]));
    *token = next_token++;
    tokens[*token].bytes = file->units * 2;
    tokens[*token].text = malloc(tokens[*token].bytes);
    assert(tokens[*token].text);
    memcpy(tokens[*token].text, file->text, tokens[*token].bytes);
    tokens[*token].refs = 1;
    live_refs++;
    return 0;
}

static int mock_tracks(void *context)
{
    if (during_import)
        during_import(context);
    return track_status;
}

static int mock_skip(void *context, int32_t bytes)
{
    struct file *file = ((struct reader *)context)->file;
    assert(bytes >= 0);
    if (!file->skip_error)
        file->offset += bytes;
    return file->skip_error;
}

static int mock_position(void *object, uint64_t *out)
{
    struct file *file = object;
    *out = file->offset;
    return file->position_error;
}

static void *mock_alloc(uint32_t bytes)
{
    if (fail_alloc)
        return NULL;
    union allocation *allocation = malloc(sizeof(*allocation) + bytes);
    assert(allocation);
    allocation->bytes = bytes;
    live_bytes += bytes;
    if (live_bytes > peak_bytes)
        peak_bytes = live_bytes;
    return allocation + 1;
}

static void mock_free(void *data)
{
    if (!data)
        return;
    union allocation *allocation = data;
    allocation--;
    assert(live_bytes >= allocation->bytes);
    live_bytes -= allocation->bytes;
    free(allocation);
}

static void mock_destroy(void *track_data)
{
    assert(track_data != owner);
    if (owner)
        return;
    assert(!string_cache);
    for (uint32_t i = 1; i < next_token; i++) {
        if (tokens[i].refs) {
            assert(frozen);
            live_refs -= tokens[i].refs;
            tokens[i].refs = 0;
            free(tokens[i].text);
            tokens[i].text = NULL;
        }
    }
    frozen = 0;
    assert(!live_refs);
}

static struct osos_media_record record(void *library, uint32_t low, uint32_t high)
{
    struct osos_media_record result = {0};
    result.data.owner = library;
    memcpy(result.data.bytes + 0x110, &low, 4);
    memcpy(result.data.bytes + 0x114, &high, 4);
    return result;
}

static void retain_field(struct reader *reader, struct osos_media_record *record,
                         const uint16_t *text, uint32_t units, uint32_t padding,
                         uint32_t type)
{
    reader->file->text = text;
    reader->file->units = units;
    uint64_t start = reader->file->offset;
    assert(cfw_album_artist_import(reader, record, 16 + units * 2 + padding, type) ==
           0);
    assert(reader->file->offset == start + 16 + units * 2 + padding);
}

static void retain(struct reader *reader, struct osos_media_record *record,
                   const uint16_t *text, uint32_t units, uint32_t padding)
{
    retain_field(reader, record, text, units, padding, 22);
}

static void expect(struct osos_media_record *record, const uint16_t *text,
                   uint32_t units)
{
    uint16_t copied[255];
    uint32_t copied_units = cfw_album_artist_read(record, copied, 255);
    assert(copied_units == units);
    if (units)
        assert(!memcmp(copied, text, units * sizeof(*text)));
}

static uint8_t library_a[0x200], library_b[0x200];
static struct osos_media_record imported;
static const uint16_t artist[] = {'A', 'r', 't', 'i', 's', 't'};
static const uint16_t other[] = {'O', 't', 'h', 'e', 'r'};

static void import_one(struct reader *reader)
{
    retain(reader, &imported, artist, 6, 3);
}

static uint32_t library_size, library_fields;
static int fail_compaction;

static uint32_t row_count(void)
{
    uint32_t rows = 0;
    if (buckets)
        for (unsigned i = 0; i < BUCKET_COUNT; i++)
            rows += buckets[i].count;
    return rows;
}

static struct osos_media_record library_record(void *library, uint32_t index)
{
    return record(library, index * 0x9e3779b9u, index >> 4);
}

static void import_library(struct reader *reader)
{
    for (uint32_t i = 0; i < library_size; i++) {
        struct osos_media_record song = library_record(reader->owner, i);
        uint32_t fields = library_fields ? library_fields : i % 7 + 1;
        if (fields & 1)
            retain(reader, &song, artist, 6, 0);
        if (fields & 2)
            retain_field(reader, &song, other, 5, 0, 29);
        if (fields & 4)
            retain_field(reader, &song, artist, 6, 0, 23);
    }
    fail_alloc = fail_compaction;
}

static void check_library(struct reader *reader, uint32_t size, uint32_t fields)
{
    library_size = size;
    library_fields = fields;
    peak_bytes = 0;
    during_import = import_library;
    assert(!cfw_album_artist_import_tracks(reader));
    during_import = NULL;
    fail_alloc = 0;
    unsigned columns = fields ? !!(fields & 1) + !!(fields & 2) + !!(fields & 4) : 3;
    uint32_t refs = 0;
    for (uint32_t i = 0; i < size; i++) {
        uint32_t present = fields ? fields : i % 7 + 1;
        refs += !!(present & 1) + !!(present & 2) + !!(present & 4);
    }
    assert(row_count() == size && live_refs == refs);
    if (fail_compaction)
        for (unsigned i = 0; i < BUCKET_COUNT; i++)
            assert(!buckets[i].count || buckets[i].capacity);
    if (size >= 10000) {
        assert(peak_bytes < size * 24u + 100000u);
        assert(live_bytes < size * (8u + columns * 4u));
    }
    for (uint32_t i = 0; i < size; i++) {
        uint32_t index = (i * 7919u) % size;
        uint32_t present = fields ? fields : index % 7 + 1;
        struct osos_media_record song = library_record(reader->owner, index);
        expect(&song, present & 1 ? artist : NULL, present & 1 ? 6 : 0);
        uint32_t album_sort = cfw_album_artist_sort_token(&song, 1);
        uint32_t artist_sort = cfw_album_artist_sort_token(&song, 0);
        assert(!!album_sort == !!(present & 2));
        assert(!!artist_sort == !!(present & 4));
        if (album_sort)
            assert(tokens[album_sort].bytes == sizeof(other) &&
                   !memcmp(tokens[album_sort].text, other, sizeof(other)));
        if (artist_sort)
            assert(tokens[artist_sort].bytes == sizeof(artist) &&
                   !memcmp(tokens[artist_sort].text, artist, sizeof(artist)));
    }
    struct osos_media_record absent = record(reader->owner, UINT32_MAX, UINT32_MAX);
    assert(!cfw_album_artist_token(&absent));
    assert(!cfw_album_artist_sort_token(&absent, 1));
    struct osos_media_record foreign = library_record(&library_a, 0);
    assert(!cfw_album_artist_token(&foreign));
    if (size >= 10000)
        printf("Album Artist memory: %u tracks, %u columns, %zu live bytes, %zu peak "
               "bytes\n",
               size, columns, live_bytes, peak_bytes);
    cfw_album_artist_destroy_tracks(reader->owner);
    assert(!live_refs && !live_bytes && !buckets);
}

/* Tokens are opaque 32-bit values. Exercise every encoded width with a nonzero base. */
static void check_packed_columns(void)
{
    struct bucket b = {0};
    struct artist_row original[12];
    for (uint32_t i = 0; i < 12; i++) {
        const uint32_t id[] = {0xa1234560u + i, 0x88888888};
        struct artist_row *row = insert_row(&b, i, id);
        assert(row);
        row->values[ALBUM_ARTIST] = 0xf0000000u ^ (i << 8);
        row->values[SORT_ALBUM_ARTIST] = 0xd0000000u ^ (i << 16);
        row->values[SORT_ARTIST] = 0x70000000u ^ (i << 24);
        original[i] = *row;
    }
    pack_bucket(&b);
    assert(!b.capacity && b.count == 12 && b.packed->offsets[COLUMN_COUNT] == 10);
    for (uint32_t i = 0; i < b.count; i++)
        for (unsigned c = 0; c < COLUMN_COUNT; c++)
            assert(value_at(&b, i, c) == original[i].values[c]);
    osos_import_free(b.packed);
    assert(!live_bytes);
}

int main(void)
{
    check_packed_columns();
    struct file file = {0};
    struct reader reader = {&file, &library_a};
    imported = record(&library_a, 42, 1);
    during_import = import_one;
    assert(cfw_album_artist_import_tracks(&reader) == 0);
    during_import = NULL;
    expect(&imported, artist, 6);
    assert(live_refs == 1);
    assert(string_cache == library_a + OSOS_TRACKDATA_ARTIST_CACHE_OFFSET);
    assert(finalized && row_count() == 1);
    assert(!cfw_album_artist_sort_token(&imported, 0));
    assert(!cfw_album_artist_import_tracks(&reader));
    retain(&reader, &imported, artist, 6, 3);

    /* Replacing metadata must preserve the native Artist/Composer reference. */
    uint32_t shared_token = cfw_album_artist_token(&imported);
    tokens[shared_token].refs++;
    live_refs++;
    retain(&reader, &imported, other, 5, 0);
    assert(tokens[shared_token].refs == 1 && live_refs == 2);
    assert(mock_release(string_cache, shared_token) == 0);

    /* Repeated and empty fields replace the same ID without leaking references. */
    retain(&reader, &imported, other, 5, 0);
    expect(&imported, other, 5);
    assert(live_refs == 1 && row_count() == 1);
    retain(&reader, &imported, NULL, 0, 0);
    expect(&imported, NULL, 0);
    assert(live_refs == 0 && row_count() == 1);

    /* Force collisions and growth; low halves alone must not identify songs. */
    struct osos_media_record records[700];
    for (uint32_t i = 0; i < 700; i++) {
        records[i] = record(&library_a, 42, i + 2);
        retain(&reader, &records[i], artist, 6, i % 4);
    }
    assert(row_count() == 701 && live_refs == 700);
    for (uint32_t i = 0; i < 700; i++)
        expect(&records[i], artist, 6);
    retain(&reader, &records[5], NULL, 0, 0);
    for (uint32_t i = 0; i < 700; i++)
        expect(&records[i], i == 5 ? NULL : artist, i == 5 ? 0 : 6);
    compact();
    assert(finalized && row_count() == 699);
    for (uint32_t i = 0; i < 700; i++)
        expect(&records[i], i == 5 ? NULL : artist, i == 5 ? 0 : 6);

    /* A fresh library import releases all retained tokens and hides old IDs. */
    reader.owner = &library_b;
    assert(cfw_album_artist_import_tracks(&reader) == 0);
    assert(live_refs == 0 && !buckets);
    expect(&records[0], NULL, 0);
    struct osos_media_record next = record(&library_b, 42, 2);
    fail_alloc = 1;
    retain(&reader, &next, artist, 6, 0);
    expect(&next, NULL, 0);
    fail_alloc = 0;
    retain(&reader, &next, other, 5, 0);
    expect(&next, other, 5);
    cfw_album_artist_destroy_tracks(&library_a);
    expect(&next, other, 5);
    expect(&records[0], NULL, 0);

    /* Optional decode failure must still consume exactly the MHOD body. */
    file.decode_error = -108;
    retain(&reader, &next, artist, 6, 3);
    expect(&next, NULL, 0);
    file.decode_error = 0;

    file.position_error = -50;
    retain(&reader, &next, artist, 6, 0);
    expect(&next, NULL, 0);
    file.position_error = 0;
    file.decode_error = -108;
    file.skip_error = -36;
    assert(cfw_album_artist_import(&reader, &next, 28, 22) == -36);
    file.decode_error = file.skip_error = 0;

    uint16_t long_name[256];
    for (unsigned i = 0; i < 254; i++)
        long_name[i] = 0x65e5;
    long_name[254] = 0xd83d;
    long_name[255] = 0xde00;
    retain(&reader, &next, long_name, 256, 0);
    expect(&next, long_name, 254);

    /* Oversized MHOD lengths must fail without issuing a negative native seek. */
    uint64_t position = file.offset;
    assert(cfw_album_artist_import(&reader, &next, UINT32_MAX, 22) == -0xd0);
    assert(file.offset == position);

    /* Both the raw token and a full-size read remain available to other patches. */
    uint16_t full[256];
    assert(cfw_album_artist_token(&next));
    assert(cfw_album_artist_read(&next, full, 256) == 256);
    assert(!memcmp(full, long_name, sizeof(full)));
    cfw_album_artist_destroy_tracks(&library_b);
    assert(!owner && !string_cache && !buckets && !live_refs);
    expect(&next, NULL, 0);
    assert(cfw_album_artist_import_tracks(&reader) == 0);

    /* Sort fields can arrive before the display name and own separate references. */
    retain_field(&reader, &next, artist, 6, 2, 29);
    retain_field(&reader, &next, other, 5, 1, 23);
    assert(live_refs == 2 && row_count() == 1);
    uint32_t album_sort = cfw_album_artist_sort_token(&next, 1);
    uint32_t artist_sort = cfw_album_artist_sort_token(&next, 0);
    assert(album_sort && artist_sort && album_sort != artist_sort);
    assert(!cfw_album_artist_token(&next));
    retain(&reader, &next, artist, 6, 0);
    assert(cfw_album_artist_token(&next) == album_sort && live_refs == 3);
    retain_field(&reader, &next, other, 5, 0, 29);
    assert(cfw_album_artist_sort_token(&next, 1) == artist_sort && live_refs == 3);
    expect(&next, artist, 6);
    file.decode_error = -108;
    retain_field(&reader, &next, NULL, 0, 4, 29);
    file.decode_error = 0;
    assert(!cfw_album_artist_sort_token(&next, 1) && live_refs == 2);
    assert(cfw_album_artist_sort_token(&next, 0) == artist_sort);
    compact();
    assert(finalized && row_count() == 1);
    expect(&next, artist, 6);
    assert(!cfw_album_artist_sort_token(&next, 1));
    assert(cfw_album_artist_sort_token(&next, 0) == artist_sort);
    cfw_album_artist_destroy_tracks(&library_b);
    assert(!live_refs && !cfw_album_artist_sort_token(&next, 0));
    assert(cfw_album_artist_import_tracks(&reader) == 0);

    /* Stock finalization freezes reference accounting; destruction frees storage. */
    retain(&reader, &next, artist, 6, 0);
    compact();
    frozen = 1;
    expect(&next, artist, 6);
    cfw_album_artist_destroy_tracks(&library_b);
    assert(!live_refs && !owner && !buckets);
    assert(cfw_album_artist_import_tracks(&reader) == 0);

    /* Import failure drops partial metadata; the next import starts empty. */
    imported = record(&library_b, 1, 1);
    during_import = import_one;
    track_status = -208;
    assert(cfw_album_artist_import_tracks(&reader) == -208);
    assert(!owner && !buckets && live_refs == 0);
    during_import = NULL;
    track_status = 0;
    assert(cfw_album_artist_import_tracks(&reader) == 0);
    clear();
    assert(live_refs == 0 && live_bytes == 0);
    assert(!cfw_album_artist_import_tracks(&reader));
    /* Fill one bucket, then fail its next growth while retaining its old rows. */
    struct osos_media_record kept = library_record(reader.owner, 0);
    retain(&reader, &kept, artist, 6, 0);
    struct bucket *full_bucket = bucket_for(persistent_id(&kept));
    struct osos_media_record skipped;
    for (uint32_t i = 1;; i++) {
        skipped = library_record(reader.owner, i);
        if (bucket_for(persistent_id(&skipped)) != full_bucket)
            continue;
        if (full_bucket->count == full_bucket->capacity)
            break;
        retain(&reader, &skipped, artist, 6, 0);
    }
    uint32_t before = live_refs;
    fail_alloc = 1;
    retain(&reader, &skipped, other, 5, 0);
    assert(row_count() == before && live_refs == before &&
           !cfw_album_artist_token(&skipped));
    fail_alloc = 0;
    compact();
    expect(&kept, artist, 6);
    clear();
    assert(!live_bytes && !live_refs);
    for (uint32_t fields = 0; fields < 8; fields++)
        check_library(&reader, 257, fields);
    for (unsigned fields = 1; fields <= 7; fields += 6) {
        check_library(&reader, 10000, fields);
        check_library(&reader, 20000, fields);
        check_library(&reader, 50000, fields);
    }
    fail_compaction = 1;
    check_library(&reader, 701, 7);
    fail_compaction = 0;
    assert(!cfw_album_artist_import_tracks(&reader));
    retain(&reader, &next, NULL, 0, 0);
    compact();
    assert(!row_count() && !buckets && !live_bytes);
    puts("Album Artist: lookup, collisions, deduplication, reloads, failures and "
         "Unicode bounds passed");
}
