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
static const uint16_t unknown[] = {'U', 'n', 'k', 'n', 'o', 'w', 'n'};

_Static_assert(sizeof(struct artist_group) == 20, "Artist group layout");
_Static_assert(sizeof(struct artwork_model) == 0x4c, "Artwork model layout");

uint32_t cfw_artists_build_original(void *);
int cfw_artist_name_original(void *, int, uint16_t *);
int cfw_artist_letter_original(void *, int, uint16_t *);
void *cfw_browser_destroy_original(void *);

static void release(void *allocation)
{
    if (allocation)
        osos_import_free(allocation);
}

static struct browser_context *context_for(void *browser)
{
    for (unsigned i = 0; i < 4; i++)
        if (browser && contexts[i].browser == browser)
            return &contexts[i];
    return NULL;
}

static void *track_data_of(void *browser)
{
    return FIELD(void *, osos_browser_owner(browser), OSOS_BROWSER_TRACKDATA_OFFSET);
}

static void cache_access(void *track_data, int acquire)
{
    if (acquire)
        osos_database_begin_update(track_data);
    else
        osos_database_end_update(track_data);
}

static uint32_t token_view(void *track_data, uint32_t token, const uint16_t **text)
{
    uint32_t bytes = 0;
    *text = NULL;
    if (osos_string_cache_view((uint8_t *)track_data +
                                   OSOS_TRACKDATA_ARTIST_CACHE_OFFSET,
                               token, text, &bytes))
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
    return osos_text_compare(NULL, left, left_units, right, right_units);
}

static uint16_t artist_letter(void *track_data, uint32_t token)
{
    const uint16_t *text;
    uint32_t units = artist_view(track_data, token, &text);
    while (units && *text <= ' ') {
        text++;
        units--;
    }
    if (!units)
        return '#';
    for (uint16_t letter = 'A'; letter <= 'Z'; letter++)
        if (!osos_text_compare(NULL, text, 1, &letter, 1))
            return letter;
    return *text < 0x80 || (*text >= 0xd800 && *text <= 0xdfff) ? '#' : *text;
}

static uint32_t group_count(void *browser)
{
    return (FIELD(uintptr_t, browser, OSOS_BROWSER_ARTISTS_OFFSET + 4) -
            FIELD(uintptr_t, browser, OSOS_BROWSER_ARTISTS_OFFSET)) /
           sizeof(struct artist_group);
}

static void clear_groups(void *browser)
{
    osos_browser_clear_artists(browser);
    osos_browser_clear_albums(browser);
    osos_browser_clear_songs(browser);
    /* Native clear retains vector capacity; these owned browsers release it too. */
    const uint32_t vectors[] = {OSOS_BROWSER_ARTISTS_OFFSET,
                                OSOS_BROWSER_ALBUMS_OFFSET};
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
        osos_model_ref_release(&((struct artwork_model *)model)->owner_reference);
        return cfw_browser_destroy_original(model);
    }
    void *browser = FIELD(void *, model, 0x40);
    struct browser_context *context = context_for(browser);
    if (context && FIELD(uint8_t, model, 0x45) == 2) {
        if (context->track_data)
            osos_browser_invalidate(browser);
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
    struct osos_media_list *list = osos_browser_current_list(browser);
    if (!track_data || !list || !osos_list_root(list))
        return 0;
    uint32_t count = osos_list_root(list)->tracks;
    if (!count)
        return 0;
    struct album_artist_track *tracks = osos_import_alloc(count * sizeof(*tracks));
    if (!tracks)
        return 0;
    cache_access(track_data, 1);
    uint32_t used = 0;
    for (uint32_t i = 0; i < count; i++) {
        struct osos_media_entry *entry = osos_list_order_entry(list, 6, 0, i);
        if (entry && entry->song.record && osos_browser_filter(browser, entry, NULL))
            tracks[used++] = (struct album_artist_track){
                entry, effective_token(entry->song.record), i};
    }
    album_artist_sort(tracks, used, compare_tokens, track_data);
    uint32_t groups = 0;
    for (uint32_t i = 0; i < used; i++)
        if (!i || compare_tokens(track_data, tracks[i - 1].token, tracks[i].token))
            groups++;
    struct artist_group *rows =
        groups ? osos_import_alloc(groups * sizeof(*rows)) : NULL;
    uint32_t *tokens = groups ? osos_import_alloc(groups * sizeof(*tokens)) : NULL;
    uint32_t *jump =
        groups ? osos_import_alloc((groups * 2 + 3) * sizeof(*jump)) : NULL;
    if (!rows || !tokens || !jump) {
        release(rows);
        release(tokens);
        release(jump);
        cache_access(track_data, 0);
        release(tracks);
        return 0;
    }
    jump[0] = 0;
    uint32_t group = 0, last_album = 0;
    struct artist_group *row = NULL;
    for (uint32_t i = 0; i < used; i++) {
        if (!i || compare_tokens(track_data, tracks[i - 1].token, tracks[i].token)) {
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
        uint32_t artwork = osos_browser_artwork(osos_browser_owner(browser), record);
        if (artwork && !row->artwork)
            row->artwork = osos_import_alloc(10 * sizeof(*row->artwork));
        if (artwork && row->artwork)
            row->artwork[row->artwork_count++] = artwork;
    }
    jump[jump[0] * 2 + 1] = 0;
    jump[jump[0] * 2 + 2] = UINT32_MAX;
    release(FIELD(void *, browser, OSOS_BROWSER_ARTISTS_OFFSET));
    FIELD(struct artist_group *, browser, OSOS_BROWSER_ARTISTS_OFFSET) = rows;
    FIELD(struct artist_group *, browser, OSOS_BROWSER_ARTISTS_OFFSET + 4) =
        rows + groups;
    FIELD(struct artist_group *, browser, OSOS_BROWSER_ARTISTS_OFFSET + 8) =
        rows + groups;
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
    struct artist_group *rows =
        FIELD(struct artist_group *, browser, OSOS_BROWSER_ARTISTS_OFFSET);
    void *track_data = track_data_of(browser);
    cache_access(track_data, 1);
    const uint16_t *text;
    uint32_t units = artist_view(track_data, rows[index].token, &text);
    if (units > 255)
        units = 255;
    if (units && text[units - 1] >= 0xd800 && text[units - 1] <= 0xdbff)
        units--;
    out[0] = units;
    for (uint32_t i = 0; i < units; i++)
        out[i + 1] = text[i];
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
    while (i + 1 < jump[0] && jump[i * 2 + 4] <= (uint32_t)index)
        i++;
    out[0] = 1;
    out[1] = jump[i * 2 + 1];
    return 0;
}

/* Called inside the native filter while TrackData cache views are pinned. */
PATCH_ARM int cfw_album_artist_compare_filter(void *browser,
                                              struct osos_media_entry *entry)
{
    if (!context_for(browser))
        return 2;
    int index = FIELD(int, browser, 0x10);
    if (index < 0)
        return 0;
    if ((uint32_t)index >= group_count(browser) || !entry->song.record)
        return 1;
    struct artist_group *rows =
        FIELD(struct artist_group *, browser, OSOS_BROWSER_ARTISTS_OFFSET);
    return compare_tokens(track_data_of(browser), rows[index].token,
                          effective_token(entry->song.record)) != 0;
}

PATCH_ARM int cfw_album_artists_album_group(void *browser, void *callback,
                                            uint32_t field_low, uint32_t field_high,
                                            uint32_t index,
                                            struct osos_media_entry *entry,
                                            struct osos_media_record *record, int first)
{
    if (!context_for(browser))
        return osos_browser_album_group(browser, callback, field_low, field_high, index,
                                        entry, record, first);
    struct osos_album_group *begin =
        FIELD(struct osos_album_group *, browser, OSOS_BROWSER_ALBUMS_OFFSET);
    struct osos_album_group *end =
        FIELD(struct osos_album_group *, browser, OSOS_BROWSER_ALBUMS_OFFSET + 4);
    if (first && begin != end)
        return 0;
    uint32_t album = FIELD(uint32_t, record, 0x34);
    if (!album)
        return 1;
    if (begin == end || end[-1].token != album) {
        struct osos_album_group row = {
            album,
            effective_token(record),
            0,
            osos_browser_artwork(osos_browser_owner(browser), record),
            0,
            ((FIELD(uint8_t, record, 0x15) & 2) >> 1) |
                ((FIELD(uint8_t, record, 0x8f) >> 7) << 1)};
        osos_album_vector_append((uint8_t *)browser + OSOS_BROWSER_ALBUMS_OFFSET, &row);
        end = FIELD(struct osos_album_group *, browser, OSOS_BROWSER_ALBUMS_OFFSET + 4);
    }
    end[-1].tracks++;
    return 1;
}

PATCH_ARM uint32_t cfw_album_artists_select_artwork(void *selection, uint32_t category)
{
    void *reference = (uint8_t *)selection + 0x18;
    void *model = osos_model_ref_get(reference);
    int custom = category == 3 && model && context_for(FIELD(void *, model, 0x40));
    /* Recreate the provider/cache even when both screens have category Albums. */
    if (custom || custom_album_artwork)
        FIELD(uint8_t, selection, OSOS_SELECTION_ARTWORK_CATEGORY_OFFSET) = 0;
    custom_album_artwork = custom;
    void *previous = artwork_model_reference;
    artwork_model_reference = custom ? reference : NULL;
    uint32_t result = osos_select_artwork_source(selection, category);
    artwork_model_reference = previous;
    return result;
}

PATCH_ARM void *cfw_album_artists_create_artwork(uint32_t kind, uint32_t selector)
{
    void *provider = osos_album_artwork_create(kind, selector);
    if (!provider || kind != 2 || selector != 0x2711 || !artwork_model_reference)
        return provider;
    void *owner = osos_model_ref_get(artwork_model_reference);
    struct artwork_model *model = osos_cpp_new(sizeof(*model));
    osos_model_init_borrowed(model, 2, FIELD(void *, owner, 0x40));
    model->owner_reference = NULL;
    osos_model_ref_assign(&model->owner_reference, artwork_model_reference);
    FIELD(uint8_t, model, 0x45) = ARTWORK_MODEL_OWNERSHIP;
    void *original = FIELD(void *, provider, 0xc);
    FIELD(void *, provider, 0xc) = model;
    osos_model_destroy(original);
    return provider;
}

PATCH_ARM void cfw_album_artists_album_subtitle(void *model, int index,
                                                struct osos_native_string *out)
{
    void *browser = FIELD(void *, model, 0x40);
    if (!context_for(browser)) {
        osos_model_album_subtitle(model, index, out);
        return;
    }
    struct osos_album_group *begin =
        FIELD(struct osos_album_group *, browser, OSOS_BROWSER_ALBUMS_OFFSET);
    struct osos_album_group *end =
        FIELD(struct osos_album_group *, browser, OSOS_BROWSER_ALBUMS_OFFSET + 4);
    if (index < 0 || (uint32_t)index >= (uint32_t)(end - begin))
        return;
    void *track_data = track_data_of(browser);
    cache_access(track_data, 1);
    const uint16_t *text;
    uint32_t units = artist_view(track_data, begin[index].artist, &text);
    osos_native_string_assign_utf16(out, text, units);
    cache_access(track_data, 0);
}

static void *acquire_browser(void **reference)
{
    /* Selection/artwork may retain the previous model after its screens close. */
    for (unsigned i = 0; i < 4; i++) {
        if (contexts[i].model_reference) {
            osos_model_ref_assign(reference, &contexts[i].model_reference);
            return osos_model_ref_get(reference);
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
    void *model = osos_model_create(0);
    void *browser = NULL;
    if (osos_browser_create(FIELD(void *, model, 0x40), &browser)) {
        osos_model_destroy(model);
        return NULL;
    }
    FIELD(void *, model, 0x40) = browser;
    FIELD(uint8_t, model, 0x45) = 2;
    osos_model_ref_init(reference, model, 0);
    unsigned slot = FIELD(uint8_t, browser, 1);
    contexts[slot] =
        (struct browser_context){browser, track_data_of(browser), *reference};
    return model;
}

static void open_browser(void *controller)
{
    void *reference = NULL;
    void *model = acquire_browser(&reference);
    if (!model)
        return;
    void *browser = FIELD(void *, model, 0x40);
    osos_library_model_prepare_artists(model);
    if (!cfw_album_artists_build(browser))
        goto done;
    void *screen = osos_create_screen_controller(osos_controller_registry(),
                                                 CFW_AlbumArtists_Screen,
                                                 CFW_AlbumArtists_Layout, 2);
    if (!screen)
        goto done;
    osos_model_ref_assign((uint8_t *)screen + 0xb4, &reference);
    FIELD(uint8_t, screen, 0xb2) = 0;
    osos_controller_push(controller, screen);
done:
    osos_model_ref_release(&reference);
}

PATCH_ARM int cfw_album_artists_action(void *controller, const char *action,
                                       uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_AlbumArtists"))
        return osos_music_action(controller, action, argument);
    if (argument != OSOS_ACTION_SUPPORT_QUERY)
        open_browser(controller);
    return 1;
}
