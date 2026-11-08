/* SPDX-License-Identifier: GPL-3.0-only */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM

#define OSOS_ARTWORK_READER_DB_OFFSET 0x10
#define OSOS_ARTWORK_IMAGE_THUMB_COUNT_OFFSET 0x22
#define OSOS_ARTWORK_IMAGE_ID_OFFSET 0x0c
#define OSOS_ARTWORK_IMAGE_DBID_OFFSET 0x10
#define OSOS_TRACK_ARTWORK_ID_OFFSET 0xf0
#define OSOS_TRACK_DBID_OFFSET 0x110
#define OSOS_ARTWORK_DB_IMAGE_COUNT_OFFSET 0x08
/* Host pointers need 8-byte alignment; the target layout uses 0x24, 0x0c, 0x10, 0x2c. */
#define OSOS_ARTWORK_IMAGE_THUMBS_OFFSET 0x28
#define OSOS_ARTWORK_DB_IMAGES_OFFSET 0x10
#define OSOS_ARTWORK_DB_LAST_IMAGE_OFFSET 0x18
#define OSOS_ARTWORK_DB_IMAGE_POOL_OFFSET 0x30

struct fake_image {
    _Alignas(8) uint8_t bytes[0x30];
};

struct fake_reader {
    _Alignas(8) uint8_t bytes[0x18];
};

struct fake_db {
    _Alignas(8) uint8_t bytes[0x38];
};

struct fake_track {
    _Alignas(8) uint8_t bytes[0x118];
};

static int fail_alloc, destroyed, parse_status, import_status;
static void *native_found;
static void *name_caches[2];
/* Thumbnail chains that the next parse attaches. */
static void *next_chain;
static uint16_t next_count;

static void *osos_import_alloc(uint32_t bytes)
{
    return fail_alloc ? NULL : malloc(bytes);
}

static void osos_import_free(void *allocation)
{
    free(allocation);
}

static void **thumbs_of(struct fake_image *image)
{
    return (void **)(image->bytes + OSOS_ARTWORK_IMAGE_THUMBS_OFFSET);
}

static uint16_t *count_of(struct fake_image *image)
{
    return (uint16_t *)(image->bytes + 0x22);
}

static int osos_artwork_image_thumbs(void *reader, void *image, uint32_t count,
                                     uint32_t flags)
{
    (void)reader, (void)count, (void)flags;
    *thumbs_of(image) = next_chain;
    *count_of(image) = next_count;
    return parse_status;
}

static void osos_artwork_thumb_destroy(void *thumb);
static void *osos_artwork_thumb_name_cache(void *thumb);

static int osos_artwork_import(void *db, uint32_t owner, const void *path, uint32_t a3,
                               uint32_t a4, uint32_t a5);

static void *osos_artwork_find_image(void *db, uint32_t id)
{
    (void)db, (void)id;
    return native_found;
}

static void *osos_artwork_find_image_by_dbid(void *db, uint32_t a1, uint32_t dbid_low,
                                             uint32_t dbid_high, uint32_t sorted)
{
    (void)db, (void)a1, (void)dbid_low, (void)dbid_high, (void)sorted;
    return native_found;
}

#include "../../../payload/artwork_thumbs.c"

static void osos_artwork_thumb_destroy(void *thumb)
{
    struct artwork_thumb *t = thumb;
    struct fake_image *image = t->image;
    struct artwork_thumb **link = (struct artwork_thumb **)thumbs_of(image);
    while (*link != t)
        link = &(*link)->next;
    *link = t->next;
    (*count_of(image))--;
    free(t);
    destroyed++;
}

/* Formats of 1000 and above use one cache per format; lower ones pick one by image. */
static void *osos_artwork_thumb_name_cache(void *thumb)
{
    struct artwork_thumb *t = thumb;
    if (t->format < 1000)
        return name_caches[((struct fake_image *)t->image)->bytes[0] & 1];
    return name_caches[t->format & 1];
}

static void (*import_body)(void *db);

static int osos_artwork_import(void *db, uint32_t owner, const void *path, uint32_t a3,
                               uint32_t a4, uint32_t a5)
{
    (void)owner, (void)path, (void)a3, (void)a4, (void)a5;
    import_body(db);
    return import_status;
}

static struct artwork_thumb *thumb(struct fake_image *image, uint32_t format,
                                   uint32_t offset, uint32_t file)
{
    struct artwork_thumb *t = calloc(1, sizeof(*t));
    t->image = image;
    t->format = format;
    t->dimensions = 0x00800080;
    t->offset = offset;
    t->size = 0x8000;
    t->file = file;
    return t;
}

/* Parses image with a two-thumbnail chain into db and returns the chain head. */
static void *parse(void *db, struct fake_image *image, uint32_t format, uint32_t offset,
                   uint32_t file)
{
    struct fake_reader reader;
    memset(&reader, 0, sizeof(reader));
    *(void **)(reader.bytes + 0x10) = db;
    struct artwork_thumb *a = thumb(image, format, offset, file);
    struct artwork_thumb *b = thumb(image, format + 2, offset * 2, file);
    a->next = b;
    next_chain = a;
    next_count = 2;
    assert(cfw_artwork_image_thumbs(&reader, image, 2, 0) == parse_status);
    return a;
}

static struct fake_image images[2200];
static struct fake_db db_marker, other_marker;
static struct pool image_pool;
static struct pool_chunk image_chunk;

#define DB_FIELD(type, db, offset) (*(type *)((db)->bytes + (offset)))
#define IMAGE_FIELD(type, image, offset) (*(type *)((image)->bytes + (offset)))

/* Appends an image to the database list, as the native image constructor does. */
static struct fake_image *add(struct fake_db *db, struct fake_image *image, uint32_t id,
                              uint32_t dbid)
{
    struct fake_image **last =
        &DB_FIELD(struct fake_image *, db, OSOS_ARTWORK_DB_LAST_IMAGE_OFFSET);
    if (*last)
        IMAGE_FIELD(struct fake_image *, *last, 0) = image;
    else
        DB_FIELD(struct fake_image *, db, OSOS_ARTWORK_DB_IMAGES_OFFSET) = image;
    *last = image;
    DB_FIELD(uint32_t, db, OSOS_ARTWORK_DB_IMAGE_COUNT_OFFSET)++;
    IMAGE_FIELD(uint32_t, image, OSOS_ARTWORK_IMAGE_ID_OFFSET) = id;
    IMAGE_FIELD(uint32_t, image, OSOS_ARTWORK_IMAGE_DBID_OFFSET) = dbid;
    IMAGE_FIELD(uint32_t, image, OSOS_ARTWORK_IMAGE_DBID_OFFSET + 4) = ~dbid;
    return image;
}

static uint32_t list_length(struct fake_db *db)
{
    uint32_t n = 0;
    for (struct fake_image *i =
             DB_FIELD(struct fake_image *, db, OSOS_ARTWORK_DB_IMAGES_OFFSET);
         i; i = IMAGE_FIELD(struct fake_image *, i, 0))
        n++;
    return n;
}

static uint32_t track_image(uint32_t id, uint32_t dbid)
{
    struct fake_track track;
    memset(&track, 0, sizeof(track));
    *(uint32_t *)(track.bytes + OSOS_TRACK_DBID_OFFSET) = dbid;
    *(uint32_t *)(track.bytes + OSOS_TRACK_DBID_OFFSET + 4) = ~dbid;
    cfw_artwork_track_image(id, &track);
    return *(uint32_t *)(track.bytes + OSOS_TRACK_ARTWORK_ID_OFFSET);
}

static void shares_duplicates(void *db)
{
    void *first = parse(db, &images[0], 1055, 0x1000, 1);
    parse(db, &images[1], 1055, 0x1000, 1);
    assert(destroyed == 2);
    assert(*thumbs_of(&images[1]) == first && *count_of(&images[1]) == 2);
    assert(*thumbs_of(&images[0]) == first && *count_of(&images[0]) == 2);

    /* Different offset, format or file: no sharing. */
    void *offset = parse(db, &images[2], 1055, 0x2000, 1);
    void *format = parse(db, &images[3], 1061, 0x1000, 1);
    void *file = parse(db, &images[4], 1055, 0x1000, 2);
    assert(destroyed == 2);
    assert(*thumbs_of(&images[2]) == offset && *thumbs_of(&images[3]) == format &&
           *thumbs_of(&images[4]) == file);

    /* Equal tokens in different name caches are different files. */
    images[17].bytes[0] = 1;
    void *low = parse(db, &images[16], 7, 0x3000, 1);
    void *high = parse(db, &images[17], 7, 0x3000, 1);
    assert(destroyed == 2);
    assert(*thumbs_of(&images[16]) == low && *thumbs_of(&images[17]) == high);

    /* A failed parse and another database stay untouched. */
    parse_status = -1;
    void *failed = parse(db, &images[5], 1055, 0x1000, 1);
    parse_status = 0;
    void *other = parse(&other_marker, &images[6], 1055, 0x1000, 1);
    assert(destroyed == 2);
    assert(*thumbs_of(&images[5]) == failed && *thumbs_of(&images[6]) == other);
}

static void grows_past_initial_capacity(void *db)
{
    for (uint32_t i = 0; i < 2000; i++)
        parse(db, &images[i], 1055, 0x10000 + i, 1);
    assert(destroyed == 0);
    for (uint32_t i = 0; i < 2000; i++) {
        void *head = *thumbs_of(&images[i]);
        parse(db, &images[2000 + i % 200], 1055, 0x10000 + i, 1);
        assert(*thumbs_of(&images[2000 + i % 200]) == head);
    }
    assert(destroyed == 4000);
    assert(capacity >= 4096);
}

static void stops_without_memory(void *db)
{
    fail_alloc = 1;
    parse(db, &images[0], 1055, 0x1000, 1);
    parse(db, &images[1], 1055, 0x1000, 1);
    assert(destroyed == 0);
    assert(disabled);
    fail_alloc = 0;
}

static void reset(void)
{
    destroyed = 0;
    memset(images, 0, sizeof(images));
    memset(&db_marker, 0, sizeof(db_marker));
    memset(&image_chunk, 0, sizeof(image_chunk));
    image_chunk.first = images[0].bytes;
    image_chunk.last = images[2199].bytes;
    image_pool.chunks = &image_chunk;
    DB_FIELD(struct pool *, &db_marker, OSOS_ARTWORK_DB_IMAGE_POOL_OFFSET) =
        &image_pool;
}

static void run(void (*body)(void *))
{
    reset();
    import_body = body;
    assert(cfw_artwork_import(&db_marker, 0, NULL, 0, 0, 0) == import_status);
    assert(!entries && !capacity && !active_db && !disabled && !last_kept);
}

static void drops_duplicates(void *db)
{
    struct fake_db *d = db;
    struct fake_image *a = add(d, &images[0], 30, 300);
    void *chain = parse(db, a, 1055, 0x1000, 1);
    parse(db, add(d, &images[1], 10, 100), 1055, 0x1000, 1);
    struct fake_image *c = add(d, &images[2], 20, 200);
    parse(db, c, 1055, 0x2000, 1);
    parse(db, add(d, &images[3], 40, 400), 1055, 0x1000, 1);
    assert(destroyed == 4);
    assert(list_length(d) == 2 && DB_FIELD(uint32_t, d, 8) == 2);
    assert(DB_FIELD(void *, d, OSOS_ARTWORK_DB_LAST_IMAGE_OFFSET) == c);
    assert(*thumbs_of(a) == chain);
    assert(image_chunk.free == 2 && image_chunk.free_list == &images[3] &&
           IMAGE_FIELD(void *, &images[3], 0) == &images[1]);
    assert(duplicate_count == 2);
    /* Photo albums resolve dropped IDs while the database loads. */
    assert(cfw_artwork_album_image(db, 10) == a);
    assert(!cfw_artwork_album_image(db, 20));
    native_found = c;
    assert(cfw_artwork_album_image(db, 20) == c);
    native_found = NULL;
}

static void keeps_unlinked_duplicates(void *db)
{
    /* An image that is not the list tail stays, with the shared chain. */
    struct fake_db *d = db;
    struct fake_image *a = add(d, &images[0], 1, 1);
    void *chain = parse(db, a, 1055, 0x1000, 1);
    struct fake_image *b = add(d, &images[1], 2, 2);
    add(d, &images[2], 3, 3);
    parse(db, b, 1055, 0x1000, 1);
    assert(list_length(d) == 3 && *thumbs_of(b) == chain && !duplicate_count);
}

static void drops_many(void *db)
{
    struct fake_db *d = db;
    /* 400 covers, five tracks each, IDs in no particular order. */
    for (uint32_t i = 0; i < 2000; i++) {
        uint32_t id = (i * 7919u) % 100003u + 1;
        parse(db, add(d, &images[i], id, id * 3), 1055, 0x10000 + i % 400, 1);
    }
    assert(list_length(d) == 400 && duplicate_count == 1600);
}

int main(void)
{
    static int caches[2];
    name_caches[0] = &caches[0];
    name_caches[1] = &caches[1];
    run(shares_duplicates);
    forget_duplicates();
    run(grows_past_initial_capacity);
    forget_duplicates();
    run(stops_without_memory);
    forget_duplicates();

    /* Tracks get the first image's ID; a track without one marks the DBID needed. */
    run(drops_duplicates);
    assert(remapping && duplicate_db == &db_marker);
    assert(track_image(10, 100) == 30 && track_image(40, 400) == 30);
    assert(track_image(20, 200) == 20 && track_image(99, 0) == 99);
    assert(track_image(0, 200) == 0 && !dbid_needed);
    assert(track_image(0, 100) == 0 && dbid_needed);
    cfw_artwork_tracks_imported();
    assert(!remapping && duplicates);
    assert(cfw_artwork_image_by_dbid(&db_marker, 0, 400, ~400u, 1) == &images[0]);
    assert(!cfw_artwork_image_by_dbid(&db_marker, 0, 200, ~200u, 1));
    assert(!cfw_artwork_image_by_dbid(&other_marker, 0, 400, ~400u, 1));
    native_found = &images[2];
    assert(cfw_artwork_image_by_dbid(&db_marker, 0, 400, ~400u, 1) == &images[2]);
    native_found = NULL;
    /* After the tracks are parsed IDs pass through unchanged. */
    assert(track_image(10, 100) == 10);

    /* Without such a track the table goes away; so does it on the next import. */
    run(drops_duplicates);
    assert(track_image(10, 100) == 30);
    cfw_artwork_tracks_imported();
    assert(!duplicates && !duplicate_db);
    assert(!cfw_artwork_image_by_dbid(&db_marker, 0, 400, ~400u, 1));

    run(drops_many);
    for (uint32_t i = 0; i < 2000; i++) {
        uint32_t id = (i * 7919u) % 100003u + 1;
        uint32_t first = ((i % 400) * 7919u) % 100003u + 1;
        assert(track_image(id, id * 3) == first);
    }
    cfw_artwork_tracks_imported();

    /* A failed import keeps nothing. */
    import_status = -1;
    run(drops_duplicates);
    import_status = 0;
    assert(!duplicates && !remapping && track_image(10, 100) == 10);

    run(keeps_unlinked_duplicates);
    assert(!duplicates);

    /* Outside an ArtworkDB import nothing is shared. */
    destroyed = 0;
    parse(&db_marker, &images[0], 1055, 0x1000, 1);
    parse(&db_marker, &images[1], 1055, 0x1000, 1);
    assert(destroyed == 0);
    return 0;
}
