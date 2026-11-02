/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Emulate native field locations independently of the host pointer width. */
#define _Static_assert(...)
#include "../../../payload/osos.h"
#define CFW_PATCH_H
#define PATCH_ARM

static int browser;
static uint8_t library[0x200];
static unsigned char list_storage;
static struct osos_media_list *media_list = (void *)&list_storage;
static struct osos_media_entry root;
static struct osos_media_order *imported_order;
static uint32_t *imported_jump, *group_tokens, *group_jump, group_total;
static void *group_begin, *group_end, *group_capacity;
static void *library_pointer = &library;
static unsigned pins, allocations;
static int fail_allocation;

struct record {
    struct osos_media_record native;
    uint32_t artist, album_artist, sort_artist, sort_album_artist, album;
    int accepted;
};
static struct record records[6];
static struct osos_media_entry entries[6];
static const uint16_t *strings[] = {
    u"",     u"The Clash", u"The Cure", u"Alpha",     u"Guest",   u"Clash",
    u"Cure", u"Zulu",      u"Aardvark", u"The Zebra", u"Theatre", u"An Artist"};

static void *field(void *object, unsigned offset)
{
    if (object == &browser) {
        switch (offset) {
        case OSOS_BROWSER_TRACKDATA_OFFSET:
            return &library_pointer;
        case OSOS_BROWSER_ARTISTS_OFFSET:
            return &group_begin;
        case OSOS_BROWSER_ARTISTS_OFFSET + 4:
            return &group_end;
        case OSOS_BROWSER_ARTISTS_OFFSET + 8:
            return &group_capacity;
        case OSOS_BROWSER_ARTIST_TOKENS_OFFSET:
            return &group_tokens;
        case OSOS_BROWSER_ARTIST_JUMP_OFFSET:
            return &group_jump;
        case OSOS_BROWSER_ARTIST_COUNT_OFFSET:
            return &group_total;
        }
    }
    if (object == media_list) {
        if (offset == OSOS_LIST_ORDER_INDEX_OFFSET + OSOS_ALBUM_ARTIST_ORDER * 4)
            return &imported_order;
        if (offset == OSOS_LIST_JUMP_INDEX_OFFSET + OSOS_ALBUM_ARTIST_ORDER * 4)
            return &imported_jump;
    }
    for (unsigned i = 0; i < 6; i++) {
        if (object != &records[i].native)
            continue;
        if (offset == 0)
            return &library_pointer;
        if (offset == 0x2c)
            return &records[i].artist;
        if (offset == 0x34)
            return &records[i].album;
    }
    abort();
}

static void *allocate(uint32_t bytes)
{
    if (fail_allocation && ++fail_allocation == 4)
        return NULL;
    void *p = malloc(bytes);
    assert(p);
    allocations++;
    return p;
}

static void deallocate(void *p)
{
    if (p) {
        assert(allocations);
        allocations--;
        free(p);
    }
}

static int view(void *cache, uint32_t token, const uint16_t **out, uint32_t *bytes)
{
    assert(cache == (uint8_t *)&library + OSOS_TRACKDATA_ARTIST_CACHE_OFFSET);
    assert(pins && token < sizeof(strings) / sizeof(*strings));
    *out = (const uint16_t *)strings[token];
    *bytes = 0;
    while (strings[token][*bytes / 2])
        *bytes += 2;
    return 0;
}

static int compare_text(void *context, const uint16_t *a, uint32_t na,
                        const uint16_t *b, uint32_t nb)
{
    (void)context;
    while (na && *a <= ' ') {
        a++;
        na--;
    }
    while (nb && *b <= ' ') {
        b++;
        nb--;
    }
    for (uint32_t i = 0; i < na && i < nb; i++) {
        uint16_t x = a[i] | 32, y = b[i] | 32;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return (na > nb) - (na < nb);
}

static struct osos_media_entry *album_entry(struct osos_media_list *source, int mode,
                                            uint32_t reverse, uint32_t index)
{
    assert(source == media_list && mode == 6 && !reverse && index < root.tracks);
    return &entries[index];
}

static int accepted(void *owner, struct osos_media_entry *entry,
                    struct osos_media_list *source)
{
    assert(owner == &browser && !source);
    return ((struct record *)entry->song.record)->accepted;
}

#define FIELD(type, object, offset) (*(type *)field(object, offset))
#define osos_browser_owner(object) (object)
#define osos_browser_current_list(object) ((void)(object), media_list)
#define osos_list_root(source) ((void)(source), &root)
#define osos_import_alloc allocate
#define osos_import_free deallocate
#define osos_string_cache_view view
#define osos_text_compare compare_text
#define osos_database_begin_update(data) ((void)(data), pins++)
#define osos_database_end_update(data) ((void)(data), pins--)
#define osos_list_order_entry album_entry
#define osos_browser_filter accepted
#define osos_browser_artwork(owner, record) ((void)(owner), (void)(record), 0)
#include "../../../payload/album_artists.c"

uint32_t cfw_album_artist_token(struct osos_media_record *record)
{
    return ((struct record *)record)->album_artist;
}

uint32_t cfw_album_artist_sort_token(struct osos_media_record *record, int album_artist)
{
    struct record *r = (void *)record;
    return album_artist ? r->sort_album_artist : r->sort_artist;
}

uint32_t cfw_artists_build_original(void *p)
{
    (void)p;
    abort();
}

int cfw_artist_name_original(void *p, int i, uint16_t *s)
{
    (void)p;
    (void)i;
    (void)s;
    abort();
}

int cfw_artist_letter_original(void *p, int i, uint16_t *s)
{
    (void)p;
    (void)i;
    (void)s;
    abort();
}

void *cfw_browser_destroy_original(void *p)
{
    (void)p;
    abort();
}

static void reset_groups(void)
{
    deallocate(group_begin);
    deallocate(group_tokens);
    deallocate(group_jump);
    group_begin = group_end = group_capacity = NULL;
    group_tokens = group_jump = NULL;
    group_total = 0;
    assert(!pins && !allocations);
}

static void expect_groups(const uint32_t *tokens, const char *letters, uint32_t count)
{
    assert(cfw_album_artists_build(&browser) == count);
    assert(!pins && group_total == count);
    struct artist_group *rows = group_begin;
    for (uint32_t i = 0; i < count; i++) {
        assert(rows[i].token == tokens[i] && group_tokens[i] == tokens[i]);
        uint16_t letter[2];
        assert(!cfw_album_artist_letter(&browser, i, letter));
        assert(letter[0] == 1 && letter[1] == (uint16_t)letters[i]);
        uint16_t name[256];
        assert(!cfw_album_artist_name(&browser, i, name));
        assert(!memcmp(name + 1, strings[tokens[i]], name[0] * 2));
    }
    assert(cfw_album_artists_build(&browser) == count);
    reset_groups();
}

int main(void)
{
    contexts[0].browser = &browser;
    root.tracks = 6;
    for (unsigned i = 0; i < 6; i++) {
        entries[i].song.record = &records[i].native;
        records[i].accepted = 1;
        records[i].artist = 4;
        records[i].album = i + 1;
    }
    records[0].album_artist = records[4].album_artist = 1;
    records[1].album_artist = 2;
    records[2].album_artist = 3;
    records[3].artist = 9; /* Artist fallback. */
    records[5].album_artist = 11;
    expect_groups((uint32_t[]){3, 11, 1, 2, 9}, "AACCZ", 5);

    /* Explicit names take priority, including a literal leading The. */
    records[0].sort_album_artist = records[1].sort_album_artist = 7;
    records[4].sort_album_artist = 8;
    records[3].sort_artist = 8;
    records[2].sort_album_artist = 9;
    expect_groups((uint32_t[]){9, 11, 3, 1, 2}, "AATZZ", 5);

    imported_order =
        malloc(sizeof(*imported_order) + 6 * sizeof(*imported_order->entries));
    assert(imported_order);
    imported_order->count = imported_order->capacity = 6;
    const unsigned order[] = {3, 0, 2, 1, 4, 5};
    for (unsigned i = 0; i < 6; i++)
        imported_order->entries[i] = &entries[order[i]];
    uint32_t native_jumps[] = {4, 0xbeef0000 | 'B', 0, 'C', 1, 'X', 2, 'Y', 3};
    imported_jump = native_jumps;
    expect_groups((uint32_t[]){9, 1, 3, 2, 11}, "BCXYY", 5);
    records[0].accepted = 0;
    expect_groups((uint32_t[]){9, 3, 2, 1, 11}, "BXYYY", 5);
    records[0].accepted = 1;

    /* Bad/missing jumps fall back to sort-name letters, retaining native order. */
    native_jumps[2] = 1;
    expect_groups((uint32_t[]){9, 1, 3, 2, 11}, "AZTZA", 5);
    imported_jump = NULL;
    expect_groups((uint32_t[]){9, 1, 3, 2, 11}, "AZTZA", 5);
    imported_order->count = 5;
    expect_groups((uint32_t[]){9, 11, 3, 1, 2}, "AATZZ", 5);
    free(imported_order);
    imported_order = NULL;

    for (unsigned i = 0; i < 6; i++)
        records[i].accepted = 0;
    expect_groups(NULL, "", 0);
    for (unsigned i = 0; i < 6; i++)
        records[i].accepted = 1;
    fail_allocation = 1;
    assert(!cfw_album_artists_build(&browser));
    fail_allocation = 0;
    reset_groups();
    return 0;
}
