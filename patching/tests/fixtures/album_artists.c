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
static int owner_model, native_artwork_model, selection, artwork_session;
static void *selected_model = &owner_model;
static uint8_t artwork_category;
static unsigned owner_references = 1, native_destroys, source_resets, source_stops;
static int source_reset_fails, source_early_return, missing_artwork_session;

struct test_model_fields {
    void *model, *browser;
    uint8_t ownership;
};
static struct test_model_fields model_fields[8];

struct test_provider {
    void *model;
};
static struct test_provider providers[4];

static void *ref_get(void *reference)
{
    return *(void **)reference;
}

static void ref_assign(void *destination, void *source)
{
    assert(!*(void **)destination && *(void **)source == &owner_model);
    *(void **)destination = *(void **)source;
    owner_references++;
}

static void ref_release(void *reference)
{
    assert(*(void **)reference == &owner_model && owner_references > 1);
    *(void **)reference = NULL;
    owner_references--;
}

static void **selection_reference(void *object)
{
    assert(object == &selection);
    return &selected_model;
}

static void *borrowed_model_init(void *model, uint8_t flags, void *source)
{
    assert(flags == 2 && source == &browser);
    for (unsigned i = 1; i < 8; i++) {
        if (!model_fields[i].model) {
            model_fields[i] = (struct test_model_fields){model, source, 3};
            return model;
        }
    }
    abort();
}

static void *create_provider(uint32_t kind, uint32_t selector)
{
    assert(kind == 2 && selector == 0x2711);
    for (unsigned i = 0; i < 4; i++) {
        if (!providers[i].model) {
            providers[i].model = &native_artwork_model;
            return &providers[i];
        }
    }
    abort();
}

static void destroy_model(void *model);

static void *get_artwork_session(void)
{
    return missing_artwork_session ? NULL : &artwork_session;
}

static void stop_artwork(void *session)
{
    assert(session == &artwork_session);
    source_stops++;
}

static int set_artwork_source(void *session, uint32_t selector);
static uint32_t select_artwork_source(void *object, uint32_t category);

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
    for (unsigned i = 0; i < 8; i++) {
        if (object && object == model_fields[i].model) {
            if (offset == 0x40)
                return &model_fields[i].browser;
            if (offset == 0x45)
                return &model_fields[i].ownership;
        }
    }
    for (unsigned i = 0; i < 4; i++)
        if (object == &providers[i] && offset == 0xc)
            return &providers[i].model;
    if (object == &selection && offset == OSOS_SELECTION_ARTWORK_CATEGORY_OFFSET)
        return &artwork_category;
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
#define osos_model_ref_get ref_get
#define osos_model_ref_assign ref_assign
#define osos_model_ref_release ref_release
#define osos_selection_reference selection_reference
#define osos_cpp_new allocate
#define osos_model_init_borrowed borrowed_model_init
#define osos_model_destroy destroy_model
#define osos_album_artwork_create create_provider
#define osos_list_artwork_session get_artwork_session
#define osos_list_artwork_stop stop_artwork
#define osos_list_artwork_set_source set_artwork_source
#define osos_select_artwork_source select_artwork_source
#include "../../../payload/album_artists.c"

static void destroy_model(void *model)
{
    if (model == &native_artwork_model) {
        native_destroys++;
        return;
    }
    cfw_album_artists_destroy_model(model);
    for (unsigned i = 1; i < 8; i++)
        if (model_fields[i].model == model)
            model_fields[i] = (struct test_model_fields){0};
    deallocate(model);
}

static int set_artwork_source(void *session, uint32_t selector)
{
    assert(session == &artwork_session && selector == 0x2711);
    assert(!artwork_model_reference);
    source_resets++;
    if (source_reset_fails)
        return -1;
    for (unsigned i = 0; i < 4; i++) {
        if (providers[i].model) {
            destroy_model(providers[i].model);
            providers[i].model = NULL;
        }
    }
    return 0;
}

static uint32_t select_artwork_source(void *object, uint32_t category)
{
    assert(object == &selection);
    if (!source_early_return && category == 3)
        assert(cfw_album_artists_create_artwork(2, 0x2711));
    return 17;
}

void *cfw_media_browser_create(void **reference)
{
    (void)reference;
    abort();
}

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
    assert(FIELD(uint8_t, p, 0x45) == 3);
    return p;
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

static void check_artwork_lifetime(void)
{
    model_fields[0] = (struct test_model_fields){&owner_model, &browser, 2};
    contexts[0].model_reference = &owner_model;
    assert(cfw_album_artists_owns_model(&owner_model));
    assert(!cfw_album_artists_owns_model(NULL));
    assert(cfw_album_artists_release_artwork() && !source_resets);
    artwork_category = 3;
    assert(cfw_album_artists_select_artwork(&selection, 3) == 17);
    assert(!artwork_category && custom_artwork_providers == 1 && owner_references == 2);
    assert(!cfw_album_artists_owns_model(providers[0].model));
    assert(native_destroys == 1 && !artwork_model_reference);

    /* A native early return must not erase evidence of a retained provider. */
    source_early_return = source_reset_fails = 1;
    artwork_category = 3;
    assert(cfw_album_artists_select_artwork(&selection, 0) == 17);
    assert(!artwork_category && custom_artwork_providers == 1 && owner_references == 2);
    assert(!cfw_album_artists_release_artwork());
    source_reset_fails = 0;
    missing_artwork_session = 1;
    assert(!cfw_album_artists_release_artwork());
    missing_artwork_session = 0;
    assert(cfw_album_artists_release_artwork());
    assert(!custom_artwork_providers && owner_references == 1 && !allocations);
    assert(contexts[0].browser == &browser &&
           contexts[0].model_reference == &owner_model);

    source_early_return = 0;
    cfw_album_artists_select_artwork(&selection, 3);
    cfw_album_artists_select_artwork(&selection, 3);
    assert(custom_artwork_providers == 2 && owner_references == 3);
    destroy_model(providers[0].model);
    providers[0].model = NULL;
    assert(custom_artwork_providers == 1 && owner_references == 2);
    source_early_return = 1;
    cfw_album_artists_select_artwork(&selection, 0);
    assert(!custom_artwork_providers && owner_references == 1 && !allocations);
    assert(source_stops == source_resets && contexts[0].browser == &browser);
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
    expect_groups((uint32_t[]){3, 11, 1, 2, 9}, "AATTT", 5);
    strings[11] = u"An Aardvark";
    expect_groups((uint32_t[]){3, 11, 1, 2, 9}, "AATTT", 5);
    strings[11] = u"A Zebra";
    expect_groups((uint32_t[]){11, 3, 1, 2, 9}, "AATTT", 5);
    strings[11] = u"An Artist";

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
    check_artwork_lifetime();
    return 0;
}

void cfw_up_next_model_destroyed(void *model)
{
    (void)model;
}

int cfw_up_next_action(void *controller, const char *action, uint32_t argument)
{
    (void)controller;
    (void)action;
    (void)argument;
    return 0;
}
