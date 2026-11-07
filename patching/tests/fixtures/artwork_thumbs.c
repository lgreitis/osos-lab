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
/* Host pointers need 8-byte alignment; the target layout uses 0x24. */
#define OSOS_ARTWORK_IMAGE_THUMBS_OFFSET 0x28

struct fake_image {
    _Alignas(8) uint8_t bytes[0x30];
};

struct fake_reader {
    _Alignas(8) uint8_t bytes[0x18];
};

static int fail_alloc, destroyed, parse_status;
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
    return 0;
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
static int db_marker, other_marker;

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

static void run(void (*body)(void *))
{
    destroyed = 0;
    memset(images, 0, sizeof(images));
    import_body = body;
    assert(cfw_artwork_import(&db_marker, 0, NULL, 0, 0, 0) == 0);
    assert(!entries && !capacity && !active_db && !disabled);
}

int main(void)
{
    static int caches[2];
    name_caches[0] = &caches[0];
    name_caches[1] = &caches[1];
    run(shares_duplicates);
    run(grows_past_initial_capacity);
    run(stops_without_memory);

    /* Outside an ArtworkDB import nothing is shared. */
    destroyed = 0;
    parse(&db_marker, &images[0], 1055, 0x1000, 1);
    parse(&db_marker, &images[1], 1055, 0x1000, 1);
    assert(destroyed == 0);
    return 0;
}
