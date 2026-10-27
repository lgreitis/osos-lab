/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Add to Playlist.
 *
 * Hold Center on a song and choose Add to Playlist. A copy of the Playlists screen
 * opens (playlist_add.ui); clicking a playlist appends the song to it and returns
 * to the song list. Songs already in that playlist are not added twice; folders
 * cannot take songs.
 *
 * The iPod never writes iTunesDB, so additions to synced playlists are kept in
 * iPod_Control\Device\CFWAdds as (playlist persistent ID, track persistent ID)
 * pairs and re-applied whenever the library is loaded: at boot and after a sync.
 * On-The-Go is saved by the iPod itself, so it is marked changed instead.
 *
 * Remove from Playlist: hold Center on a song inside a playlist. Removing a song
 * that came from the computer is kept in CFWRemoves the same way and re-applied
 * after each load; removing a song added on the iPod just forgets that addition.
 * The playlist that is playing, On-The-Go (which has its own Remove) and folders
 * do not offer it.
 */
#include "osos.h"
#include "cfw.h"
#include "patch.h"
#include "preferences.h"
#include "patches/sites.h"
#include "play_next.h"
#include "playlist_add.h"
#include "playlist_add_ui.h"
#include "playlist_rename.h"

#define ADDS_MAGIC 0x41574643u    /* "CFWA" */
#define REMOVES_MAGIC 0x52574643u /* "CFWR" */
#define PAIRS_VERSION 2
#define PAIRS_VERSION_ID_ONLY 1
#define PAIRS_MAX 256

/* list_name: hash of the playlist's own (pre-rename) name, for playlists made on
   the iPod, whose persistent ID does not last from one load to the next. */
struct saved_pair {
    uint32_t list[2];
    uint32_t track[2];
    uint32_t list_name;
    uint32_t reserved;
};

/* Version 1 entries: IDs only. */
struct saved_pair_id_only {
    uint32_t list[2];
    uint32_t track[2];
};

/* (playlist persistent ID, track persistent ID) pairs, one fixed-size file each. */
struct pair_file {
    uint32_t magic, version, count;
    struct saved_pair entries[PAIRS_MAX];
};

static struct pair_file adds, removes;

/* Saved pairs whose playlist was not there yet at the last load: playlists made
   on the iPod are loaded later (they are not in iTunesDB), so these are applied
   when the playlist is first seen (cfw_playlist_seen). */
static uint8_t waiting[2][PAIRS_MAX];
static uint32_t waiting_count;

static const struct osos_path adds_path = {0, "iPod_Control\\Device\\CFWAdds"};
static const struct osos_path removes_path = {0, "iPod_Control\\Device\\CFWRemoves"};

/* Native playlist row reference built by OSOS_PLAYLISTS_ROW_ITEM. */
struct playlist_ref {
    uintptr_t *vtable;
    struct osos_media_list *list;
    uint32_t reserved[6];
};

/* Media database seen at the last library load; NULL until then. */
static uint8_t *database;
/* Song chosen from a hold menu, waiting for a playlist. */
static struct osos_media_entry *pending;

static const uint32_t *word_pair(const void *object, unsigned offset)
{
    return (const uint32_t *)((const uint8_t *)object + offset);
}

static const uint32_t *list_id(struct osos_media_list *list)
{
    return word_pair(list, OSOS_PLAYLIST_PERSISTENT_ID);
}

static const uint32_t *track_id(struct osos_media_entry *entry)
{
    return word_pair(entry->song.record, OSOS_RECORD_PERSISTENT_ID);
}

static int is_list(struct osos_media_list *list)
{
    return list && *word_pair(list, 4) == OSOS_LIST_MAGIC;
}

static int contains(struct osos_media_list *list, struct osos_media_record *record)
{
    struct osos_media_entry *root = osos_list_root(list);
    struct osos_media_entry *entry = root->group.first;
    for (unsigned i = 0; i < root->tracks && entry; i++, entry = entry->next)
        if (entry->song.record == record)
            return 1;
    return 0;
}

static struct osos_media_list *otg_list(void)
{
    /* Media database objects have type byte 1 (see MediaBrowser_GetOwner). */
    if (!database || database[0] != 1)
        return NULL;
    return *(struct osos_media_list **)(database + OSOS_DATABASE_OTG_LIST);
}

static void mark_otg_changed(void)
{
    uint8_t *state = *(uint8_t **)(database + OSOS_DATABASE_OTG_STATE);
    if (state)
        state[OSOS_OTG_STATE_DIRTY] = 1;
}

/* Caller holds the player lock (NULL while loading). 1 when the song is in the
   list afterwards. */
static int append(struct osos_player *player, struct osos_media_list *list,
                  struct osos_media_entry *entry)
{
    if (!is_list(list) || !entry || !entry->song.record || !osos_list_root(list))
        return 0;
    if (osos_list_root(list)->groups)
        return 0;
    if (contains(list, entry->song.record))
        return 1;
    if (!cfw_list_append(player, list, entry))
        return 0;
    /* Lists cache their song count ("N Songs", and the row count the song list
       sizes its artist lines by); this byte makes the next read recount, as the
       native list reference's mark-changed method (0x0827aa2c) does. */
    *((volatile uint8_t *)list + OSOS_LIST_COUNTS_STALE) = 1;
    return 1;
}

static const struct osos_path *pair_path(struct pair_file *file)
{
    return file == &adds ? &adds_path : &removes_path;
}

__attribute__((optimize("no-tree-loop-distribute-patterns")))
static void pair_load(struct pair_file *file, uint32_t magic)
{
    if (cfw_preferences_read(pair_path(file), file, sizeof(*file)) &&
        file->magic == magic && file->version == PAIRS_VERSION &&
        file->count <= PAIRS_MAX) {
        file->magic = magic;
        return;
    }
    /* Version 1: read into the same storage, then widen from the last entry
       down so each one is read before anything overwrites it. */
    struct saved_pair_id_only *old = (struct saved_pair_id_only *)file->entries;
    if (!cfw_preferences_read(pair_path(file), file, 12 + PAIRS_MAX * sizeof(*old)) ||
        file->magic != magic || file->version != PAIRS_VERSION_ID_ONLY ||
        file->count > PAIRS_MAX)
        file->count = 0;
    for (uint32_t i = file->count; i-- > 0;) {
        uint32_t l0 = old[i].list[0], l1 = old[i].list[1];
        uint32_t t0 = old[i].track[0], t1 = old[i].track[1];
        struct saved_pair *pair = &file->entries[i];
        pair->list[0] = l0;
        pair->list[1] = l1;
        pair->track[0] = t0;
        pair->track[1] = t1;
        pair->list_name = 0;
        pair->reserved = 0;
    }
    file->magic = magic;
    file->version = PAIRS_VERSION;
}

/* A saved pair's playlist: the same persistent ID, or the same own name (an
   iPod-made playlist's ID changes from one load to the next). */
static int same_list(const struct saved_pair *pair, const uint32_t *list, uint32_t name)
{
    if ((list[0] || list[1]) && pair->list[0] == list[0] && pair->list[1] == list[1])
        return 1;
    return pair->list_name && pair->list_name == name;
}

static int pair_index(struct pair_file *file, const uint32_t *list, uint32_t name,
                      const uint32_t *track)
{
    for (uint32_t i = 0; i < file->count; i++) {
        struct saved_pair *pair = &file->entries[i];
        if (same_list(pair, list, name) && pair->track[0] == track[0] &&
            pair->track[1] == track[1])
            return (int)i;
    }
    return -1;
}

static void pair_add(struct pair_file *file, const uint32_t *list, uint32_t name,
                     const uint32_t *track)
{
    if (pair_index(file, list, name, track) >= 0 || file->count == PAIRS_MAX)
        return;
    struct saved_pair *pair = &file->entries[file->count++];
    pair->list[0] = list[0];
    pair->list[1] = list[1];
    pair->track[0] = track[0];
    pair->track[1] = track[1];
    pair->list_name = name;
    pair->reserved = 0;
    cfw_preferences_write(pair_path(file), file, sizeof(*file));
}

/* 1 when the pair was saved (and is now dropped). */
static int pair_drop(struct pair_file *file, const uint32_t *list, uint32_t name,
                     const uint32_t *track)
{
    int i = pair_index(file, list, name, track);
    if (i < 0)
        return 0;
    /* Field-wise: a struct copy would need memcpy, which the payload lacks. */
    int removal = file == &removes;
    if (waiting[removal][i])
        waiting_count--;
    waiting[removal][i] = waiting[removal][file->count - 1];
    waiting[removal][file->count - 1] = 0;
    struct saved_pair *pair = &file->entries[i], *last = &file->entries[--file->count];
    pair->list[0] = last->list[0];
    pair->list[1] = last->list[1];
    pair->track[0] = last->track[0];
    pair->track[1] = last->track[1];
    pair->list_name = last->list_name;
    pair->reserved = last->reserved;
    cfw_preferences_write(pair_path(file), file, sizeof(*file));
    return 1;
}

/* FNV-1a over the playlist's own (pre-rename) name; 0 when it has none. */
static uint32_t own_name_hash(struct osos_media_list *list)
{
    uint16_t name[64];
    cfw_playlist_own_name(list, name, 63);
    uint32_t hash = 2166136261u;
    for (uint16_t i = 1; i <= name[0]; i++)
        hash = (hash ^ name[i]) * 16777619u;
    return name[0] && hash ? hash : 0;
}

static int saved_ids(struct osos_media_list *list, struct osos_media_record *record,
                     const uint32_t **playlist, uint32_t *name, const uint32_t **track)
{
    *playlist = list_id(list);
    *name = own_name_hash(list);
    *track = word_pair(record, OSOS_RECORD_PERSISTENT_ID);
    return ((*playlist)[0] || (*playlist)[1] || *name) && ((*track)[0] || (*track)[1]);
}

/* Added on the iPod: undo a saved removal of a synced song, else save the add. */
static void remember(struct osos_media_list *list, struct osos_media_entry *entry)
{
    const uint32_t *playlist, *track;
    uint32_t name;
    if (!saved_ids(list, entry->song.record, &playlist, &name, &track))
        return;
    if (!pair_drop(&removes, playlist, name, track))
        pair_add(&adds, playlist, name, track);
}

/* Removed on the iPod: forget a saved add, else save the removal. */
static void forget(struct osos_media_list *list, struct osos_media_record *record)
{
    const uint32_t *playlist, *track;
    uint32_t name;
    if (!saved_ids(list, record, &playlist, &name, &track))
        return;
    if (!pair_drop(&adds, playlist, name, track))
        pair_add(&removes, playlist, name, track);
}

static struct osos_media_entry *entry_for_record(struct osos_media_list *list,
                                                 struct osos_media_record *record)
{
    struct osos_media_entry *root = osos_list_root(list);
    if (!root || root->groups)
        return NULL;
    struct osos_media_entry *entry = root->group.first;
    for (unsigned i = 0; i < root->tracks && entry; i++, entry = entry->next)
        if (entry->song.record == record)
            return entry;
    return NULL;
}

static struct osos_media_entry *entry_for_track(struct osos_media_list *list,
                                                const uint32_t *id)
{
    struct osos_media_entry *root = osos_list_root(list);
    if (!root || root->groups)
        return NULL;
    struct osos_media_entry *entry = root->group.first;
    for (unsigned i = 0; i < root->tracks && entry; i++, entry = entry->next) {
        if (!entry->song.record)
            continue;
        const uint32_t *track = word_pair(entry->song.record, OSOS_RECORD_PERSISTENT_ID);
        if (track[0] == id[0] && track[1] == id[1])
            return entry;
    }
    return NULL;
}

/* Native playlist item delete ('dpli'), then the playlist-changed notice ('cpld')
   and a recount, as the list reference's remove-row routine (0x0827a8c8) does. */
static void delete_entry(struct osos_media_list *list, struct osos_media_entry *entry)
{
    typedef int (*delete_item)(struct osos_media_entry *);
    typedef void (*changed)(struct osos_media_list *);
    ((delete_item)OSOS_PLAYLIST_ITEM_DELETE)(entry);
    ((changed)OSOS_PLAYLIST_CHANGED)(list);
    *((volatile uint8_t *)list + OSOS_LIST_COUNTS_STALE) = 1;
}

static struct osos_media_list *next_list(struct osos_media_list *list)
{
    typedef struct osos_media_list *(*next)(struct osos_media_list *);
    return ((next)OSOS_TRACK_DATA_NEXT_LIST)(list);
}

/* Library entry for a saved track: the first list holding it (normally the
   library list, which is walked first). */
static struct osos_media_entry *find_track(struct osos_media_list *first,
                                           const uint32_t *id)
{
    unsigned lists = 0;
    for (struct osos_media_list *list = first; is_list(list) && lists < 4096;
         list = next_list(list), lists++) {
        struct osos_media_entry *root = osos_list_root(list);
        if (!root || root->groups)
            continue;
        struct osos_media_entry *entry = root->group.first;
        for (unsigned i = 0; i < root->tracks && entry; i++, entry = entry->next) {
            if (!entry->song.record)
                continue;
            const uint32_t *track = track_id(entry);
            if (track[0] == id[0] && track[1] == id[1])
                return entry;
        }
    }
    return NULL;
}

static struct osos_media_list *find_list(struct osos_media_list *first,
                                         const uint32_t *id)
{
    unsigned lists = 0;
    for (struct osos_media_list *list = first; is_list(list) && lists < 4096;
         list = next_list(list), lists++) {
        const uint32_t *playlist = list_id(list);
        if (playlist[0] == id[0] && playlist[1] == id[1])
            return list;
    }
    return NULL;
}

static struct osos_media_list *first_list(void)
{
    uint8_t *tracks = database ? *(uint8_t **)(database + OSOS_DATABASE_TRACK_DATA) : NULL;
    if (!tracks || *word_pair(tracks, 4) != OSOS_TRACK_DATA_MAGIC)
        return NULL;
    return *(struct osos_media_list **)(tracks + OSOS_TRACK_DATA_FIRST_LIST);
}

/* The playlist a saved pair belongs to in this load. */
static struct osos_media_list *saved_list(struct osos_media_list *first,
                                          const struct saved_pair *pair)
{
    struct osos_media_list *found =
        pair->list[0] || pair->list[1] ? find_list(first, pair->list) : NULL;
    if (found || !pair->list_name)
        return found;
    unsigned lists = 0;
    for (struct osos_media_list *list = first; is_list(list) && lists < 4096;
         list = next_list(list), lists++)
        if (own_name_hash(list) == pair->list_name)
            return list;
    return NULL;
}


static void apply_pair(struct osos_media_list *first, struct osos_media_list *list,
                       int removal, const struct saved_pair *pair)
{
    if (removal) {
        struct osos_media_entry *entry = entry_for_track(list, pair->track);
        if (entry)
            delete_entry(list, entry);
    } else {
        struct osos_media_entry *entry = find_track(first, pair->track);
        if (entry)
            append(NULL, list, entry);
    }
}

static void apply_saved(void)
{
    /* Load first, so later changes keep the saved lists even if nothing applies. */
    pair_load(&adds, ADDS_MAGIC);
    pair_load(&removes, REMOVES_MAGIC);
    struct osos_media_list *first = first_list();
    if (!first)
        return;
    /* The library is still loading: nothing plays yet, so no player is needed.
       Removals first, then additions. */
    waiting_count = 0;
    for (int removal = 1; removal >= 0; removal--) {
        struct pair_file *file = removal ? &removes : &adds;
        for (uint32_t i = 0; i < file->count; i++) {
            struct osos_media_list *list = saved_list(first, &file->entries[i]);
            waiting[removal][i] = !list && file->entries[i].list_name;
            waiting_count += waiting[removal][i];
            if (list)
                apply_pair(first, list, removal, &file->entries[i]);
        }
    }
}

/* Called from the playlist name getter, i.e. whenever a playlist is listed. */
void cfw_playlist_seen(void *seen)
{
    static int busy;
    struct osos_media_list *list = seen;
    if (!waiting_count || busy || !is_list(list) || !osos_list_root(list))
        return;
    struct osos_media_list *first = first_list();
    if (!first)
        return;
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    int playing = player->playback.list == list;
    osos_player_unlock(player);
    if (playing)
        return;
    busy = 1;
    uint32_t name = own_name_hash(list);
    for (int removal = 1; removal >= 0 && name; removal--) {
        struct pair_file *file = removal ? &removes : &adds;
        for (uint32_t i = 0; i < file->count; i++) {
            if (!waiting[removal][i] || file->entries[i].list_name != name)
                continue;
            waiting[removal][i] = 0;
            waiting_count--;
            apply_pair(first, list, removal, &file->entries[i]);
        }
    }
    busy = 0;
}


static int list_exists(const uint32_t *id)
{
    struct osos_media_list *first = first_list();
    return first && find_list(first, id) != NULL;
}

/* Both library loads (boot and after a sync). */
PATCH_ARM int cfw_database_load(void *media_database)
{
    typedef int (*load)(void *);
    pending = NULL;
    int result = ((load)OSOS_DATABASE_LOAD)(media_database);
    database = media_database;
    if (database) {
        /* Renames first: iPod-made playlists are found by their own names. */
        cfw_playlist_names_reconcile(list_exists);
        apply_saved();
    }
    return result;
}

/* Playlist a song list (Playlists > a playlist) is showing: its browser's current
   list, resolved to the library's own list with that persistent ID. */
static struct osos_media_list *shown_list(void *controller)
{
    typedef void *(*get_model)(void *);
    typedef void (*ref_init)(struct playlist_ref *, void *, int);
    typedef void (*ref_destroy)(struct playlist_ref *);
    typedef void (*current)(void *, struct playlist_ref *);
    void *model = ((get_model)OSOS_LIBRARY_MODEL_REF_GET)(
        (uint8_t *)controller + OSOS_NESTED_PLAYLISTS_BROWSER);
    if (!model)
        return NULL;
    struct playlist_ref ref;
    ((ref_init)OSOS_LIST_REF_INIT)(&ref, NULL, 0);
    ((current)osos_method(model, OSOS_BROWSER_CURRENT_LIST_REF))(model, &ref);
    struct osos_media_list *list = ref.list;
    ((ref_destroy)OSOS_LIST_REF_DESTROY)(&ref);
    if (!is_list(list))
        return NULL;
    const uint32_t *id = list_id(list);
    struct osos_media_list *first = first_list();
    struct osos_media_list *library = first && (id[0] || id[1]) ? find_list(first, id) : NULL;
    return library ? library : list;
}

static int removable(struct osos_media_list *list)
{
    struct osos_media_entry *root = list ? osos_list_root(list) : NULL;
    if (!root || root->groups || list == otg_list() ||
        (*word_pair(list, OSOS_LIST_KIND_FLAGS) & OSOS_LIST_KIND_LIBRARY))
        return 0;
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    int playing = player->playback.list == list;
    osos_player_unlock(player);
    return !playing;
}

/* Song list controller whose hold menu is being built (Playlists > a playlist). */
static void *menu_controller;

PATCH_ARM int cfw_nested_playlists_menu(void *controller, int row)
{
    typedef int (*build)(void *, int);
    menu_controller = controller;
    int result = ((build)OSOS_NESTED_PLAYLISTS_BUILD_MENU)(controller, row);
    menu_controller = NULL;
    return result;
}

void cfw_playlist_add_menu(struct osos_context_menu *menu)
{
    osos_context_menu_append(menu, CFW_ADD_TO_PLAYLIST, "CFW_AddToPlaylist", 1);
    if (menu_controller && removable(shown_list(menu_controller)))
        osos_context_menu_append(menu, CFW_REMOVE_FROM_PLAYLIST, "CFW_RemoveFromPlaylist",
                                 1);
}


/* The list view (TListView) showing the current song list: the list UI model's
   container (+0x38) cast to the list view interface. */
static void *current_list_view(void)
{
    typedef void *(*get)(void);
    typedef void *(*cast)(void *, uint32_t);
    uint8_t *ui = ((get)OSOS_LIST_VIEW_MODEL)();
    void *container = ui ? *(void **)(ui + OSOS_LIST_UI_CONTAINER) : NULL;
    return container ? ((cast)OSOS_INTERFACE_CAST)(container, OSOS_LIST_VIEW_INTERFACE)
                     : NULL;
}

/* A removal waiting for its row's blink to finish before the list reloads. */
static struct {
    void *controller, *view;
    struct osos_media_list *list;
    int row;
} reload;

static void reload_song_list(void)
{
    typedef void *(*get_model)(void *);
    typedef void (*set_current)(void *, void *);
    typedef void *(*instance)(void);
    typedef void (*refresh)(void *, int);
    typedef void (*select_row)(void *, int, int);
    void *controller = reload.controller;
    int row = reload.row;
    struct osos_media_entry *root = osos_list_root(reload.list);
    reload.controller = reload.view = NULL;
    /* As the native On-The-Go removal does (0x0822fb98): the browser takes the
       controller's playlist reference again, then the Playlists model refreshes. */
    void *model = ((get_model)OSOS_LIBRARY_MODEL_REF_GET)(
        (uint8_t *)controller + OSOS_NESTED_PLAYLISTS_BROWSER);
    if (model)
        ((set_current)osos_method(model, OSOS_BROWSER_SET_CURRENT_LIST_REF))(
            model, (uint8_t *)controller + OSOS_NESTED_PLAYLISTS_LIST_REF);
    void *playlists = ((instance)OSOS_PLAYLISTS_GET)();
    if (playlists)
        ((refresh)osos_method(playlists, OSOS_PLAYLISTS_REFRESH))(playlists, 0);
    /* The reload puts the highlight on the first row: move it to the song that
       took the removed one's place (or the new last song). */
    int count = root ? root->tracks : 0;
    if (row >= count)
        row = count - 1;
    void *view = current_list_view();
    if (view && row > 0)
        ((select_row)osos_method(view, OSOS_LIST_VIEW_SELECT_ROW))(view, row, 1);
}

/* End of a list view's row blink (it sends the blink's list event here). */
PATCH_ARM void cfw_list_blink_done(void *view, void *event, int flags)
{
    typedef void (*send)(void *, void *, int);
    ((send)OSOS_VIEW_SEND_EVENT)(view, event, flags);
    if (!reload.controller)
        return;
    /* Only the blink that the removal started counts. Any other blink (picking
       a song blinks its row too) means the song list is gone or moved on, so
       drop the reload rather than touch a controller that may no longer exist. */
    if (reload.view == view && current_list_view() == view)
        reload_song_list();
    else
        reload.controller = reload.view = NULL;
}

void cfw_playlist_remove(void *controller, struct osos_media_item *item)
{
    typedef void *(*get_ui)(void);
    typedef int (*highlighted)(void *);
    struct osos_media_record *record =
        !item ? NULL : item->record ? item->record
                                    : (osos_media_item_entry(item)
                                           ? osos_media_item_entry(item)->song.record
                                           : NULL);
    struct osos_media_list *list = record ? shown_list(controller) : NULL;
    if (!list || !removable(list))
        return;
    struct osos_media_entry *entry = entry_for_record(list, record);
    if (!entry)
        return;
    void *ui = ((get_ui)OSOS_LIST_VIEW_MODEL)();
    int row = ui ? ((highlighted)osos_method(ui, OSOS_LIST_VIEW_HIGHLIGHTED))(ui) : -1;
    void *view = current_list_view();
    /* A song picked for Add to Playlist may be this very entry. */
    if (pending == entry)
        pending = NULL;
    delete_entry(list, entry);
    forget(list, record);
    reload.controller = controller;
    reload.view = view;
    reload.list = list;
    reload.row = row;
    /* Blink the row the way the iPod does for On-The-Go: the old row stays on
       screen while it blinks, and the list reloads when the blink ends. */
    if (row >= 0 && view)
        ((void (*)(int))OSOS_LIST_FLASH_ROW)(row);
    else
        reload_song_list();
}

/* Cover Flow album whose backside the song came from; -1 for other screens. */
static int coverflow_album = -1;

void cfw_playlist_add_select(struct osos_media_item *item)
{
    pending = item ? osos_media_item_entry(item) : NULL;
    coverflow_album = -1;
}

void cfw_playlist_add_from_coverflow(int album)
{
    coverflow_album = album;
}

/* The Playlists chooser reconfigures the library browser that Cover Flow's back
   side lists from; point it at the album again before returning to it, as the
   flip to the back side does (Cover Flow model vtable +0xdc). */
static void show_coverflow_album(int album)
{
    typedef void *(*get)(void);
    typedef void (*show_album)(void *, int);
    void *model = album >= 0 ? ((get)OSOS_COVERFLOW_MODEL_GET)() : NULL;
    if (!model)
        return;
    show_album show = (show_album)osos_method(model, OSOS_COVERFLOW_MODEL_SHOW_ALBUM);
    /* Clear first, as flipping back to the front does: that resets the browser to
       the album list, so the album index means the same album again even after
       an add's library refresh left the browser somewhere else. */
    show(model, -1);
    show(model, album);
}

/* Album to list again once Cover Flow is shown after the chooser closes: an add
   changes the library, and its change notices re-point the browser after the
   chooser's own handler has run. */
static int coverflow_reshow = -1;

static void restore_coverflow(void)
{
    int album = coverflow_album;
    coverflow_album = -1;
    coverflow_reshow = album;
    show_coverflow_album(album);
}

int cfw_coverflow_appear_original(void *controller, int first);

PATCH_ARM int cfw_coverflow_appear(void *controller, int first)
{
    int result = cfw_coverflow_appear_original(controller, first);
    int album = coverflow_reshow;
    coverflow_reshow = -1;
    /* The first showing clears the back side itself; only a return re-lists. */
    if (!first)
        show_coverflow_album(album);
    return result;
}

static void emit(void *controller, const char *event)
{
    typedef void *(*init)(void *, const char *, void *);
    typedef void (*destroy)(void *);
    typedef void (*navigate)(void *, void *);
    uint32_t name[2], scratch[2];
    ((init)OSOS_CPP_STRING_INIT)(name, event, scratch);
    ((navigate)osos_method(controller, OSOS_CONTROLLER_EMIT_EVENT))(controller, name);
    ((destroy)OSOS_CPP_STRING_DESTROY)(name);
}

static struct osos_media_list *chosen_list(void *controller)
{
    typedef void *(*model)(void);
    typedef int (*highlighted)(void *);
    typedef int (*map_row)(void *, int);
    typedef void *(*instance)(void);
    typedef void *(*row_item)(struct playlist_ref *, void *, int);
    void *view = ((model)OSOS_LIST_VIEW_MODEL)();
    if (!view)
        return NULL;
    int row = ((highlighted)osos_method(view, OSOS_LIST_VIEW_HIGHLIGHTED))(view);
    if (row < 0)
        return NULL;
    row = ((map_row)osos_method(controller, OSOS_MEDIA_LIST_MAP_ROW))(controller, row);
    if (row < 0)
        return NULL;
    struct playlist_ref ref;
    ref.vtable = NULL;
    ref.list = NULL;
    ((row_item)OSOS_PLAYLISTS_ROW_ITEM)(&ref, ((instance)OSOS_PLAYLISTS_GET)(), row);
    return ref.list;
}

int cfw_playlist_add_action(void *controller, const char *action, uint32_t argument)
{
    if (cfw_string_equal(action, "CFW_AddToPlaylistCancel")) {
        /* playlist_add.ui closes the chooser after this. */
        if (argument != OSOS_ACTION_SUPPORT_QUERY) {
            pending = NULL;
            restore_coverflow();
        }
        return 1;
    }
    if (!cfw_string_equal(action, "CFW_AddToPlaylistChosen"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    struct osos_media_entry *entry = pending;
    pending = NULL;
    struct osos_media_list *list = entry ? chosen_list(controller) : NULL;
    if (list) {
        struct osos_player *player = osos_player_get();
        osos_player_lock(player);
        int added = append(player, list, entry);
        osos_player_unlock(player);
        if (added) {
            if (list == otg_list())
                mark_otg_changed();
            else
                remember(list, entry);
        }
    }
    restore_coverflow();
    /* playlist_add.ui binds controller.CFW_AddedToPlaylist to PopTopScreen. */
    emit(controller, "CFW_AddedToPlaylist");
    return 1;
}
