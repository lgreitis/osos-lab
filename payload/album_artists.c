/* SPDX-License-Identifier: GPL-3.0-only */
#include "album_artists.h"
#include "album_artist.h"
#include "album_artists_index.h"
#include "album_artists_ui.h"
#include "cfw.h"
#include "patch.h"

#define FIELD(type, object, offset) (*(type *)((uint8_t *)(object) + (offset)))

struct artist_group {
    uint32_t token, display_token;
    uint32_t *artwork;
    uint32_t artwork_count, albums;
};
struct album_group {
    uint32_t token, artist, display_artist;
    uint32_t artwork;
    uint32_t tracks, flags;
};
struct browser_context {
    void *browser, *track_data;
    /* Non-owning control block; the model destructor clears this context. */
    void *model_reference;
};
static struct browser_context contexts[4];
static void *artwork_model_reference;
static int custom_album_artwork;
struct artwork_model {
    uint8_t native[0x48];
    /* The native artwork cache can outlive the screen that owns this browser. */
    void *owner_reference;
};
#define ARTWORK_MODEL_OWNERSHIP 0x80
static const uint16_t unknown[] = {'U','n','k','n','o','w','n'};

_Static_assert(sizeof(struct artist_group) == 20, "Artist group layout");
_Static_assert(sizeof(struct album_group) == 24, "Album group layout");
_Static_assert(sizeof(struct artwork_model) == 0x4c, "Artwork model layout");

uint32_t cfw_artists_build_original(void *);
int cfw_artist_name_original(void *, int, uint16_t *);
int cfw_artist_letter_original(void *, int, uint16_t *);
void *cfw_browser_destroy_original(void *);

static void *allocate(uint32_t bytes)
{
    return ((void *(*)(uint32_t))OSOS_IMPORT_ALLOC)(bytes);
}

static void release(void *allocation)
{
    if (allocation)
        ((void (*)(void *))OSOS_IMPORT_FREE)(allocation);
}

static struct browser_context *context_for(void *browser)
{
    for (unsigned i = 0; i < 4; i++)
        if (browser && contexts[i].browser == browser)
            return &contexts[i];
    return NULL;
}

static void *owner_of(void *browser)
{
    return ((void *(*)(void *))OSOS_BROWSER_OWNER)(browser);
}

static void *track_data_of(void *browser)
{
    return FIELD(void *, owner_of(browser), OSOS_BROWSER_TRACKDATA_OFFSET);
}

static void cache_access(void *track_data, int acquire)
{
    ((void (*)(void *))(acquire ? OSOS_DATABASE_BEGIN_UPDATE :
                                  OSOS_DATABASE_END_UPDATE))(track_data);
}

static uint32_t token_view(void *track_data, uint32_t token, const uint16_t **text)
{
    uint32_t bytes = 0;
    *text = NULL;
    typedef int (*view)(void *, uint32_t, const uint16_t **, uint32_t *);
    if (((view)OSOS_STRING_CACHE_VIEW)((uint8_t *)track_data +
            OSOS_TRACKDATA_ARTIST_CACHE_OFFSET, token, text, &bytes))
        return 0;
    return bytes / 2;
}

static uint32_t effective_token(struct osos_media_record *record)
{
    const uint16_t *text;
    uint32_t token = cfw_album_artist_token(record);
    if (token && token_view(FIELD(void *, record, 0), token, &text))
        return token;
    token = FIELD(uint32_t, record, 0x2c);
    return token && token_view(FIELD(void *, record, 0), token, &text) ? token : 0;
}

static uint32_t artist_view(void *track_data, uint32_t token, const uint16_t **text)
{
    uint32_t units = token_view(track_data, token, text);
    if (!units) {
        *text = unknown;
        units = sizeof(unknown) / sizeof(*unknown);
    }
    return units;
}

static int compare_tokens(void *track_data, uint32_t a, uint32_t b)
{
    if (a == b)
        return 0;
    const uint16_t *left, *right;
    uint32_t left_units = artist_view(track_data, a, &left);
    uint32_t right_units = artist_view(track_data, b, &right);
    typedef int (*compare)(void *, const uint16_t *, uint32_t, const uint16_t *, uint32_t);
    return ((compare)OSOS_TEXT_COMPARE)(NULL, left, left_units, right, right_units);
}

static uint16_t artist_letter(void *track_data, uint32_t token)
{
    const uint16_t *text;
    uint32_t units = artist_view(track_data, token, &text);
    while (units && *text <= ' ') { text++; units--; }
    if (!units)
        return '#';
    typedef int (*compare)(void *, const uint16_t *, uint32_t, const uint16_t *, uint32_t);
    for (uint16_t letter = 'A'; letter <= 'Z'; letter++)
        if (!((compare)OSOS_TEXT_COMPARE)(NULL, text, 1, &letter, 1))
            return letter;
    return *text < 0x80 || (*text >= 0xd800 && *text <= 0xdfff) ? '#' : *text;
}

static uint32_t group_count(void *browser)
{
    return (FIELD(uintptr_t, browser, OSOS_BROWSER_ARTISTS_OFFSET + 4) - FIELD(uintptr_t, browser, OSOS_BROWSER_ARTISTS_OFFSET)) /
           sizeof(struct artist_group);
}

static void clear_groups(void *browser)
{
    ((void (*)(void *))OSOS_BROWSER_CLEAR_ARTISTS)(browser);
    ((void (*)(void *))OSOS_BROWSER_CLEAR_ALBUMS)(browser);
    ((void (*)(void *))OSOS_BROWSER_CLEAR_SONGS)(browser);
    /* Native clear retains vector capacity; these owned browsers release it too. */
    const uint32_t vectors[] = {OSOS_BROWSER_ARTISTS_OFFSET, OSOS_BROWSER_ALBUMS_OFFSET};
    for (unsigned i = 0; i < 2; i++) {
        release(FIELD(void *, browser, vectors[i]));
        for (unsigned j = 0; j < 3; j++)
            FIELD(void *, browser, vectors[i] + j * 4) = NULL;
    }
    FIELD(uint32_t, browser, OSOS_BROWSER_ARTIST_COUNT_OFFSET) = 0;
}

void cfw_album_artists_invalidate(void *track_data)
{
    for (unsigned i = 0; i < 4; i++) {
        struct browser_context *context = &contexts[i];
        if (!context->browser || context->track_data != track_data)
            continue;
        clear_groups(context->browser);
        /* TrackData destruction has already unlinked its lists. */
        FIELD(void *, context->browser, 4) = NULL;
        context->track_data = NULL;
    }
}

PATCH_ARM void *cfw_album_artists_destroy_model(void *model)
{
    if (FIELD(uint8_t, model, 0x45) == ARTWORK_MODEL_OWNERSHIP) {
        FIELD(uint8_t, model, 0x45) = 3;
        ((void (*)(void *))OSOS_MODEL_REF_RELEASE)(
            &((struct artwork_model *)model)->owner_reference);
        return cfw_browser_destroy_original(model);
    }
    void *browser = FIELD(void *, model, 0x40);
    struct browser_context *context = context_for(browser);
    if (context && FIELD(uint8_t, model, 0x45) == 2) {
        if (context->track_data)
            ((int (*)(void *))OSOS_BROWSER_INVALIDATE)(browser);
        clear_groups(browser);
        *context = (struct browser_context){0};
    }
    return cfw_browser_destroy_original(model);
}

PATCH_ARM uint32_t cfw_album_artists_build(void *browser)
{
    struct browser_context *context = context_for(browser);
    if (!context)
        return cfw_artists_build_original(browser);
    if (FIELD(void *, browser, OSOS_BROWSER_ARTIST_TOKENS_OFFSET))
        return group_count(browser);
    void *track_data = track_data_of(browser);
    context->track_data = track_data;
    typedef struct osos_media_list *(*current_list)(void *);
    struct osos_media_list *list = ((current_list)OSOS_BROWSER_CURRENT_LIST)(browser);
    if (!track_data || !list || !osos_list_root(list))
        return 0;
    uint32_t count = osos_list_root(list)->tracks;
    if (!count)
        return 0;
    struct album_artist_track *tracks = allocate(count * sizeof(*tracks));
    if (!tracks)
        return 0;
    cache_access(track_data, 1);
    uint32_t used = 0;
    for (uint32_t i = 0; i < count; i++) {
        typedef struct osos_media_entry *(*get_entry)(void *, int, uint32_t, uint32_t);
        struct osos_media_entry *entry = ((get_entry)OSOS_LIST_ORDER_ENTRY)(list, 6, 0, i);
        typedef int (*matches)(void *, struct osos_media_entry *, void *);
        if (entry && entry->song.record &&
                ((matches)OSOS_BROWSER_FILTER)(browser, entry, NULL))
            tracks[used++] = (struct album_artist_track){entry,
                effective_token(entry->song.record), i};
    }
    album_artist_sort(tracks, used, compare_tokens, track_data);
    uint32_t groups = 0;
    for (uint32_t i = 0; i < used; i++)
        if (!i || compare_tokens(track_data, tracks[i-1].token, tracks[i].token))
            groups++;
    struct artist_group *rows = groups ? allocate(groups * sizeof(*rows)) : NULL;
    uint32_t *tokens = groups ? allocate(groups * sizeof(*tokens)) : NULL;
    uint32_t *jump = groups ? allocate((groups * 2 + 3) * sizeof(*jump)) : NULL;
    if (!rows || !tokens || !jump) {
        release(rows); release(tokens); release(jump);
        cache_access(track_data, 0);
        release(tracks);
        return 0;
    }
    jump[0] = 0;
    uint32_t group = 0, last_album = 0;
    struct artist_group *row = NULL;
    for (uint32_t i = 0; i < used; i++) {
        if (!i || compare_tokens(track_data, tracks[i-1].token, tracks[i].token)) {
            row = &rows[group];
            *row = (struct artist_group){tracks[i].token, 0, NULL, 0, 0};
            tokens[group] = row->token;
            uint16_t letter = artist_letter(track_data, row->token);
            if (!jump[0] || jump[jump[0] * 2 - 1] != letter) {
                jump[jump[0] * 2 + 1] = letter;
                jump[jump[0] * 2 + 2] = group;
                jump[0]++;
            }
            group++;
            last_album = 0;
        }
        struct osos_media_record *record = tracks[i].entry->song.record;
        uint32_t album = FIELD(uint32_t, record, 0x34);
        if (!album || album == last_album)
            continue;
        last_album = album;
        row->albums++;
        if (row->artwork_count == 10)
            continue;
        typedef uint32_t (*art)(void *, void *);
        uint32_t artwork = ((art)OSOS_BROWSER_ARTWORK)(owner_of(browser), record);
        if (artwork && !row->artwork)
            row->artwork = allocate(10 * sizeof(*row->artwork));
        if (artwork && row->artwork)
            row->artwork[row->artwork_count++] = artwork;
    }
    jump[jump[0] * 2 + 1] = 0;
    jump[jump[0] * 2 + 2] = UINT32_MAX;
    release(FIELD(void *, browser, OSOS_BROWSER_ARTISTS_OFFSET));
    FIELD(struct artist_group *, browser, OSOS_BROWSER_ARTISTS_OFFSET) = rows;
    FIELD(struct artist_group *, browser, OSOS_BROWSER_ARTISTS_OFFSET + 4) = rows + groups;
    FIELD(struct artist_group *, browser, OSOS_BROWSER_ARTISTS_OFFSET + 8) = rows + groups;
    FIELD(uint32_t *, browser, OSOS_BROWSER_ARTIST_TOKENS_OFFSET) = tokens;
    FIELD(uint32_t *, browser, OSOS_BROWSER_ARTIST_JUMP_OFFSET) = jump;
    FIELD(uint32_t, browser, OSOS_BROWSER_ARTIST_COUNT_OFFSET) = groups;
    cache_access(track_data, 0);
    release(tracks);
    return groups;
}

PATCH_ARM int cfw_album_artist_name(void *browser, int index, uint16_t *out)
{
    if (!context_for(browser))
        return cfw_artist_name_original(browser, index, out);
    out[0] = 0;
    if (index < 0 || (uint32_t)index >= group_count(browser))
        return -50;
    struct artist_group *rows = FIELD(struct artist_group *, browser, OSOS_BROWSER_ARTISTS_OFFSET);
    void *track_data = track_data_of(browser);
    cache_access(track_data, 1);
    const uint16_t *text;
    uint32_t units = artist_view(track_data, rows[index].token, &text);
    if (units > 255) units = 255;
    if (units && text[units-1] >= 0xd800 && text[units-1] <= 0xdbff) units--;
    out[0] = units;
    for (uint32_t i = 0; i < units; i++) out[i+1] = text[i];
    cache_access(track_data, 0);
    return 0;
}

PATCH_ARM int cfw_album_artist_letter(void *browser, int index, uint16_t *out)
{
    if (!context_for(browser))
        return cfw_artist_letter_original(browser, index, out);
    out[0] = 0;
    if (index < 0 || (uint32_t)index >= group_count(browser))
        return -50;
    uint32_t *jump = FIELD(uint32_t *, browser, OSOS_BROWSER_ARTIST_JUMP_OFFSET);
    uint32_t i = 0;
    while (i + 1 < jump[0] && jump[i * 2 + 4] <= (uint32_t)index) i++;
    out[0] = 1;
    out[1] = jump[i * 2 + 1];
    return 0;
}

/* Called inside the native filter while TrackData cache views are pinned. */
PATCH_ARM int cfw_album_artist_compare_filter(void *browser, struct osos_media_entry *entry)
{
    if (!context_for(browser))
        return 2;
    int index = FIELD(int, browser, 0x10);
    if (index < 0)
        return 0;
    if ((uint32_t)index >= group_count(browser) || !entry->song.record)
        return 1;
    struct artist_group *rows = FIELD(struct artist_group *, browser, OSOS_BROWSER_ARTISTS_OFFSET);
    return compare_tokens(track_data_of(browser), rows[index].token,
                          effective_token(entry->song.record)) != 0;
}

PATCH_ARM int cfw_album_artists_album_group(void *browser, void *callback,
    uint32_t field_low, uint32_t field_high, uint32_t index,
    struct osos_media_entry *entry, struct osos_media_record *record, int first)
{
    typedef int (*native)(void *, void *, uint32_t, uint32_t, uint32_t,
                         struct osos_media_entry *, struct osos_media_record *, int);
    if (!context_for(browser))
        return ((native)OSOS_BROWSER_ALBUM_GROUP)(browser, callback, field_low,
                                                 field_high, index, entry, record, first);
    struct album_group *begin = FIELD(struct album_group *, browser, OSOS_BROWSER_ALBUMS_OFFSET);
    struct album_group *end = FIELD(struct album_group *, browser, OSOS_BROWSER_ALBUMS_OFFSET + 4);
    if (first && begin != end)
        return 0;
    uint32_t album = FIELD(uint32_t, record, 0x34);
    if (!album)
        return 1;
    if (begin == end || end[-1].token != album) {
        typedef uint32_t (*art)(void *, void *);
        struct album_group row = {album, effective_token(record), 0,
            ((art)OSOS_BROWSER_ARTWORK)(owner_of(browser), record), 0,
            ((FIELD(uint8_t, record, 0x15) & 2) >> 1) |
            ((FIELD(uint8_t, record, 0x8f) >> 7) << 1)};
        typedef void (*append)(void *, struct album_group *);
        ((append)OSOS_ALBUM_VECTOR_APPEND)((uint8_t *)browser + OSOS_BROWSER_ALBUMS_OFFSET, &row);
        end = FIELD(struct album_group *, browser, OSOS_BROWSER_ALBUMS_OFFSET + 4);
    }
    end[-1].tracks++;
    return 1;
}

PATCH_ARM uint32_t cfw_album_artists_select_artwork(void *selection, uint32_t category)
{
    void *reference = (uint8_t *)selection + 0x18;
    void *model = ((void *(*)(void *))OSOS_MODEL_REF_GET)(reference);
    int custom = category == 3 && model && context_for(FIELD(void *, model, 0x40));
    /* Recreate the provider/cache even when both screens have category Albums. */
    if (custom || custom_album_artwork)
        FIELD(uint8_t, selection, OSOS_SELECTION_ARTWORK_CATEGORY_OFFSET) = 0;
    custom_album_artwork = custom;
    void *previous = artwork_model_reference;
    artwork_model_reference = custom ? reference : NULL;
    uint32_t result = ((uint32_t (*)(void *, uint32_t))OSOS_SELECT_ARTWORK_SOURCE)(
        selection, category);
    artwork_model_reference = previous;
    return result;
}

PATCH_ARM void *cfw_album_artists_create_artwork(uint32_t kind, uint32_t selector)
{
    void *provider = ((void *(*)(uint32_t, uint32_t))OSOS_ALBUM_ARTWORK_CREATE)(kind, selector);
    if (!provider || kind != 2 || selector != 0x2711 || !artwork_model_reference)
        return provider;
    void *owner = ((void *(*)(void *))OSOS_MODEL_REF_GET)(artwork_model_reference);
    struct artwork_model *model = ((void *(*)(uint32_t))OSOS_CPP_NEW)(sizeof(*model));
    typedef void *(*borrow)(void *, uint8_t, void *);
    ((borrow)OSOS_MODEL_INIT_BORROWED)(model, 2, FIELD(void *, owner, 0x40));
    model->owner_reference = NULL;
    ((void (*)(void *, void *))OSOS_MODEL_REF_ASSIGN)(
        &model->owner_reference, artwork_model_reference);
    FIELD(uint8_t, model, 0x45) = ARTWORK_MODEL_OWNERSHIP;
    void *original = FIELD(void *, provider, 0xc);
    FIELD(void *, provider, 0xc) = model;
    ((void (*)(void *))osos_method(original, 0x1c))(original);
    return provider;
}

PATCH_ARM void cfw_album_artists_album_subtitle(void *model, int index,
                                               struct osos_native_string *out)
{
    void *browser = FIELD(void *, model, 0x40);
    typedef void (*native)(void *, int, struct osos_native_string *);
    if (!context_for(browser)) {
        ((native)OSOS_MODEL_ALBUM_SUBTITLE)(model, index, out);
        return;
    }
    struct album_group *begin = FIELD(struct album_group *, browser, OSOS_BROWSER_ALBUMS_OFFSET);
    struct album_group *end = FIELD(struct album_group *, browser, OSOS_BROWSER_ALBUMS_OFFSET + 4);
    if (index < 0 || (uint32_t)index >= (uint32_t)(end - begin))
        return;
    void *track_data = track_data_of(browser);
    cache_access(track_data, 1);
    const uint16_t *text;
    uint32_t units = artist_view(track_data, begin[index].artist, &text);
    osos_native_string_assign_utf16(out, text, units);
    cache_access(track_data, 0);
}

static void destroy_model(void *model)
{
    ((void (*)(void *))osos_method(model, 0x1c))(model);
}

static void *acquire_browser(void **reference)
{
    /* Selection/artwork may retain the previous model after its screens close. */
    for (unsigned i = 0; i < 4; i++) {
        if (contexts[i].model_reference) {
            ((void (*)(void *, void *))OSOS_MODEL_REF_ASSIGN)(
                reference, &contexts[i].model_reference);
            return ((void *(*)(void *))OSOS_MODEL_REF_GET)(reference);
        }
    }
    /* Reserve one browser for the whole navigation stack, including child screens. */
    void **registry = (void **)OSOS_BROWSER_REGISTRY;
    unsigned available = 0;
    for (unsigned i = 0; i < 4; i++)
        available += registry[i] == NULL;
    /* Leave a slot for native auxiliary browsing. */
    if (available < 2)
        return NULL;
    typedef void *(*create_model)(uint32_t);
    void *model = ((create_model)OSOS_MODEL_CREATE)(0);
    void *browser = NULL;
    typedef int (*create_browser)(void *, void **);
    if (((create_browser)OSOS_BROWSER_CREATE)(FIELD(void *, model, 0x40), &browser)) {
        destroy_model(model);
        return NULL;
    }
    FIELD(void *, model, 0x40) = browser;
    FIELD(uint8_t, model, 0x45) = 2;
    typedef void (*ref_init)(void **, void *, int);
    ((ref_init)OSOS_MODEL_REF_INIT)(reference, model, 0);
    unsigned slot = FIELD(uint8_t, browser, 1);
    contexts[slot] = (struct browser_context){browser, track_data_of(browser), *reference};
    return model;
}

static void open_browser(void *controller)
{
    void *reference = NULL;
    void *model = acquire_browser(&reference);
    if (!model)
        return;
    void *browser = FIELD(void *, model, 0x40);
    ((void (*)(void *))osos_method(model, 0x38))(model);
    ((void (*)(void *))osos_method(model, 0xd8))(model);
    ((void (*)(void *, uint32_t, uint32_t))osos_method(model, 0x5c))(model, 0x11, 0);
    if (!cfw_album_artists_build(browser))
        goto done;
    typedef void *(*registry_fn)(void);
    typedef void *(*create_screen)(void *, uint32_t, uint32_t, uint32_t);
    void *screen = ((create_screen)OSOS_CREATE_SCREEN_CONTROLLER)(
        ((registry_fn)OSOS_CONTROLLER_REGISTRY)(), CFW_AlbumArtists_Screen,
        CFW_AlbumArtists_Layout, 2);
    if (!screen)
        goto done;
    ((void (*)(void *, void *))OSOS_MODEL_REF_ASSIGN)((uint8_t *)screen + 0xb4, &reference);
    FIELD(uint8_t, screen, 0xb2) = 0;
    ((void (*)(void *, void *))osos_method(controller, 0xd0))(controller, screen);
done:
    ((void (*)(void *))OSOS_MODEL_REF_RELEASE)(&reference);
}

PATCH_ARM int cfw_album_artists_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_AlbumArtists"))
        return ((int (*)(void *, const char *, uint32_t))OSOS_MUSIC_ACTION)(controller, action, argument);
    if (argument != OSOS_ACTION_SUPPORT_QUERY)
        open_browser(controller);
    return 1;
}
