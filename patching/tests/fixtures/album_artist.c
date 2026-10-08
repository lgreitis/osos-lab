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
    union { void *owner; uint8_t bytes[0x118]; } data;
};
struct osos_media_item { struct osos_media_record *record; };
struct file {
    uint64_t offset;
    const uint16_t *text;
    uint32_t units;
    int decode_error, skip_error, position_error;
};
struct reader { struct file *file; void *owner; };
static void mock_destroy(void *);
static int mock_release(void *, uint32_t);
static int mock_read(void *, uint32_t, void *, uint32_t *, uint32_t);
static int mock_decode(void *, uint32_t, uint32_t, void *, uint32_t *, uint32_t, int);
static int mock_tracks(void *);
static int mock_skip(void *, int32_t);
static int mock_position(void *, uint64_t *);
static void *mock_alloc(uint32_t);
static void mock_free(void *);
#define OSOS_TRACKDATA_DESTROY_STORAGE mock_destroy
#define OSOS_TRACKDATA_ARTIST_CACHE_OFFSET 0x118
#define OSOS_IMPORT_STRING_CACHE_RELEASE mock_release
#define OSOS_IMPORT_STRING_CACHE_READ mock_read
#define OSOS_ITUNES_IMPORT_STRING mock_decode
#define OSOS_ITUNES_IMPORT_TRACKS mock_tracks
#define OSOS_ITUNES_SKIP_BYTES mock_skip
#define OSOS_IMPORT_FILE_POSITION mock_position
#define OSOS_IMPORT_ALLOC mock_alloc
#define OSOS_IMPORT_FREE mock_free
#define OSOS_ITUNES_READER_FILE_OFFSET offsetof(struct reader, file)
#define OSOS_ITUNES_READER_OWNER_OFFSET offsetof(struct reader, owner)
#include "../../../payload/album_artist.c"

void cfw_album_artists_invalidate(void *track_data)
{
    assert(track_data == owner);
}

struct cached { uint16_t *text; uint32_t bytes, refs; };
static struct cached tokens[4096];
static uint32_t next_token = 1, live_refs;
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

static int mock_read(void *cache, uint32_t token, void *out, uint32_t *bytes, uint32_t limit)
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
    assert(encoding == 1 && type == 22 && cache == string_cache && glyph_field == 0 && collect == 1);
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

static void *mock_alloc(uint32_t bytes) { return fail_alloc ? NULL : malloc(bytes); }
static void mock_free(void *data) { free(data); }
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

static void retain(struct reader *reader, struct osos_media_record *record,
                   const uint16_t *text, uint32_t units, uint32_t padding)
{
    reader->file->text = text;
    reader->file->units = units;
    uint64_t start = reader->file->offset;
    assert(cfw_album_artist_import(reader, record, 16 + units * 2 + padding) == 0);
    assert(reader->file->offset == start + 16 + units * 2 + padding);
}

static void expect(struct osos_media_record *record, const uint16_t *text, uint32_t units)
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
static void import_one(struct reader *reader) { retain(reader, &imported, artist, 6, 3); }

int main(void)
{
    struct file file = {0};
    struct reader reader = {&file, &library_a};
    imported = record(&library_a, 42, 1);
    during_import = import_one;
    assert(cfw_album_artist_import_tracks(&reader) == 0);
    during_import = NULL;
    expect(&imported, artist, 6);
    assert(live_refs == 1);
    assert(string_cache == library_a + OSOS_TRACKDATA_ARTIST_CACHE_OFFSET);

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
    assert(live_refs == 1 && count == 1);
    retain(&reader, &imported, NULL, 0, 0);
    expect(&imported, NULL, 0);
    assert(live_refs == 0 && count == 1);

    /* Force collisions and growth; low halves alone must not identify songs. */
    struct osos_media_record records[700];
    for (uint32_t i = 0; i < 700; i++) {
        records[i] = record(&library_a, 42, i + 2);
        retain(&reader, &records[i], artist, 6, i % 4);
    }
    assert(capacity >= 1024 && count == 701 && live_refs == 700);
    for (uint32_t i = 0; i < 700; i++)
        expect(&records[i], artist, 6);
    retain(&reader, &records[5], NULL, 0, 0);
    for (uint32_t i = 0; i < 700; i++)
        expect(&records[i], i == 5 ? NULL : artist, i == 5 ? 0 : 6);

    /* A fresh library import releases all retained tokens and hides old IDs. */
    reader.owner = &library_b;
    assert(cfw_album_artist_import_tracks(&reader) == 0);
    assert(live_refs == 0 && !capacity);
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
    assert(cfw_album_artist_import(&reader, &next, 28) == -36);
    file.decode_error = file.skip_error = 0;

    uint16_t long_name[256];
    for (unsigned i = 0; i < 254; i++) long_name[i] = 0x65e5;
    long_name[254] = 0xd83d;
    long_name[255] = 0xde00;
    retain(&reader, &next, long_name, 256, 0);
    expect(&next, long_name, 254);

    /* Oversized MHOD lengths must fail without issuing a negative native seek. */
    uint64_t position = file.offset;
    assert(cfw_album_artist_import(&reader, &next, UINT32_MAX) == -0xd0);
    assert(file.offset == position);

    /* Both the raw token and a full-size read remain available to other patches. */
    uint16_t full[256];
    assert(cfw_album_artist_token(&next));
    assert(cfw_album_artist_read(&next, full, 256) == 256);
    assert(!memcmp(full, long_name, sizeof(full)));
    cfw_album_artist_destroy_tracks(&library_b);
    assert(!owner && !string_cache && !capacity && !live_refs);
    expect(&next, NULL, 0);
    assert(cfw_album_artist_import_tracks(&reader) == 0);

    /* Stock finalization freezes reference accounting; destruction frees storage. */
    retain(&reader, &next, artist, 6, 0);
    frozen = 1;
    expect(&next, artist, 6);
    cfw_album_artist_destroy_tracks(&library_b);
    assert(!live_refs && !owner && !capacity);
    assert(cfw_album_artist_import_tracks(&reader) == 0);

    /* Import failure drops partial metadata; the next import starts empty. */
    imported = record(&library_b, 1, 1);
    during_import = import_one;
    track_status = -208;
    assert(cfw_album_artist_import_tracks(&reader) == -208);
    assert(!owner && !capacity && live_refs == 0);
    during_import = NULL;
    track_status = 0;
    assert(cfw_album_artist_import_tracks(&reader) == 0);
    clear();
    assert(live_refs == 0);
    puts("Album Artist: lookup, collisions, deduplication, reloads, failures and Unicode bounds passed");
}
