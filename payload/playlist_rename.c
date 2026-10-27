/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Rename Playlist.
 *
 * Hold Center on a playlist and choose Rename Playlist. The stock Search screen
 * opens with its click-wheel keyboard (see playlist_rename.ui). While renaming:
 *   - typed text is title-cased (first letter and each letter after a space);
 *   - pressing Menu renames the playlist to the typed text and returns to
 *     Playlists. Menu with nothing typed cancels.
 *
 * The Search keyboard keeps its text in the search model's 'Str ' 0x7684
 * resource; every keystroke rewrites it through OSOS_MODEL_RESOURCE_SET. The
 * playlist is renamed in memory with the native set-name routine used when
 * On-The-Go and Genius playlists are saved.
 *
 * Renames persist in iPod_Control\Device\CFWNames, one entry per playlist keyed
 * by its iTunesDB persistent ID. The iPod never writes iTunesDB, so after a reboot
 * or sync the library brings back the computer's name; the playlist name getter is
 * hooked to show the saved name instead. Playlists made on the iPod fall back to
 * matching by their own name, since they have no lasting ID.
 */
#include "osos.h"
#include "cfw.h"
#include "patch.h"
#include "preferences.h"
#include "patches/sites.h"
#include "playlist_rename.h"
#include "playlist_rename_ui.h"
#include "playlist_add.h"

#define STR_TYPE 0x53747220u /* 'Str ' */
#define NAME_MAX 255

/* Native playlist row reference built by OSOS_PLAYLISTS_ROW_ITEM. */
struct playlist_ref {
    uintptr_t *vtable;
    void *list;
    uint32_t reserved[6];
};

/* Length-prefixed UTF-16 name, as passed to OSOS_PLAYLIST_SET_NAME. */
struct native_name {
    uint16_t length;
    uint16_t text[NAME_MAX];
};

/* Saved renames, keyed by the iTunesDB playlist persistent ID (mhyp +0x1c, which
   the importer stores at list +0x30). Playlists made on the iPod are not in
   iTunesDB: they get no ID or a new one at each load, so each entry also keeps the
   playlist's own name ("Playlist 1") and falls back to it when its ID is not in
   the library (see cfw_playlist_names_reconcile). Names are length-prefixed
   UTF-16. */
#define SAVED_MAX 32
#define SAVED_UNITS 63
#define SAVED_MAGIC 0x4e574643u /* "CFWN" */
#define SAVED_VERSION 3
#define SAVED_VERSION_ID_ONLY 2

struct saved_name {
    uint32_t id[2];
    uint16_t renamed[SAVED_UNITS + 1];
    /* Library (or iPod-given) name when renamed; matched when orphaned. */
    uint16_t original[SAVED_UNITS + 1];
    uint32_t orphan;
};

/* Version 2 entries: ID and new name only. */
struct saved_name_id_only {
    uint32_t id[2];
    uint16_t renamed[SAVED_UNITS + 1];
};

static struct {
    uint32_t magic, version, count;
    struct saved_name entries[SAVED_MAX];
} saved;

static const struct osos_path saved_path = {0, "iPod_Control\\Device\\CFWNames"};

int cfw_playlist_get_name_original(void *list, struct native_name *name);

static const uint32_t *playlist_id(void *list)
{
    return (const uint32_t *)((const uint8_t *)list + OSOS_PLAYLIST_PERSISTENT_ID);
}

static int same_name(const uint16_t *a, const uint16_t *b)
{
    for (uint16_t i = 0; i <= a[0]; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

/* No libc in the payload: keep GCC from turning this copy loop into memcpy. */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
static void copy_name(uint16_t *to, const uint16_t *from, uint16_t limit)
{
    uint16_t n = from[0] < limit ? from[0] : limit;
    to[0] = n;
    for (uint16_t i = 1; i <= n; i++)
        to[i] = from[i];
}

static struct saved_name *saved_by_id(const uint32_t *id)
{
    if (!id[0] && !id[1])
        return NULL;
    for (uint32_t i = 0; i < saved.count; i++)
        if (!saved.entries[i].orphan && saved.entries[i].id[0] == id[0] &&
            saved.entries[i].id[1] == id[1])
            return &saved.entries[i];
    return NULL;
}

/* original: the playlist's name before renaming (from the unhooked getter). */
static struct saved_name *saved_for(void *list, const uint16_t *original)
{
    const uint32_t *id = playlist_id(list);
    struct saved_name *entry = saved_by_id(id);
    if (entry || !original || !original[0])
        return entry;
    for (uint32_t i = 0; i < saved.count; i++) {
        entry = &saved.entries[i];
        if ((entry->orphan || (!entry->id[0] && !entry->id[1])) && entry->original[0] &&
            same_name(entry->original, original)) {
            /* Adopt this load's ID in memory so later lookups match directly; the
               file keeps the old one, and the next load reconciles again. */
            entry->id[0] = id[0];
            entry->id[1] = id[1];
            entry->orphan = !id[0] && !id[1];
            return entry;
        }
    }
    return NULL;
}

/* Boot: loaded from cfw_preferences_hook with the other CFW settings. */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
void cfw_playlist_names_load(void)
{
    if (cfw_preferences_read(&saved_path, &saved, sizeof(saved)) &&
        saved.magic == SAVED_MAGIC && saved.version == SAVED_VERSION &&
        saved.count <= SAVED_MAX) {
        for (uint32_t i = 0; i < saved.count; i++)
            saved.entries[i].orphan = 0;
    } else {
        /* Earlier files: read into the same storage, then widen each entry from
           the last one down so no entry is overwritten before it is moved. */
        struct saved_name_id_only *old = (struct saved_name_id_only *)saved.entries;
        uint32_t size = 12 + SAVED_MAX * sizeof(*old);
        if (!cfw_preferences_read(&saved_path, &saved, size) ||
            saved.magic != SAVED_MAGIC || saved.version != SAVED_VERSION_ID_ONLY ||
            saved.count > SAVED_MAX)
            saved.count = 0;
        for (uint32_t i = saved.count; i-- > 0;) {
            struct saved_name *entry = &saved.entries[i];
            uint32_t id0 = old[i].id[0], id1 = old[i].id[1];
            uint16_t renamed[SAVED_UNITS + 1];
            copy_name(renamed, old[i].renamed, SAVED_UNITS);
            entry->id[0] = id0;
            entry->id[1] = id1;
            copy_name(entry->renamed, renamed, SAVED_UNITS);
            entry->original[0] = 0;
            entry->orphan = 0;
        }
    }
    saved.magic = SAVED_MAGIC;
    saved.version = SAVED_VERSION;
}

/* After each library load: entries whose playlist ID is gone match by name. */
void cfw_playlist_names_reconcile(int (*exists)(const uint32_t *id))
{
    for (uint32_t i = 0; i < saved.count; i++) {
        struct saved_name *entry = &saved.entries[i];
        entry->orphan = (!entry->id[0] && !entry->id[1]) || !exists(entry->id);
    }
}

/* The playlist's own name, before any rename on the iPod (length-prefixed). */
void cfw_playlist_own_name(void *list, uint16_t *name, uint16_t limit)
{
    struct native_name current;
    current.length = 0;
    cfw_playlist_get_name_original(list, &current);
    struct saved_name *entry = saved_for(list, &current.length);
    copy_name(name, entry && entry->original[0] ? entry->original : &current.length,
              limit);
}

/* One entry per playlist, overwritten on every rename; the whole fixed-size file
   is rewritten in place. Renaming back to the original name drops the entry. */
static void remember(void *list, const struct native_name *renamed)
{
    struct native_name library;
    if (renamed->length > SAVED_UNITS)
        return;
    library.length = 0;
    cfw_playlist_get_name_original(list, &library);
    struct saved_name *entry = saved_for(list, &library.length);
    /* Compare with the name before the first rename, not an earlier rename. */
    if (same_name(entry && entry->original[0] ? entry->original : &library.length,
                  &renamed->length)) {
        if (!entry)
            return;
        struct saved_name *last = &saved.entries[--saved.count];
        if (entry != last) {
            entry->id[0] = last->id[0];
            entry->id[1] = last->id[1];
            copy_name(entry->renamed, last->renamed, SAVED_UNITS);
            copy_name(entry->original, last->original, SAVED_UNITS);
            entry->orphan = last->orphan;
        }
    } else {
        if (!entry) {
            if (saved.count == SAVED_MAX)
                return;
            entry = &saved.entries[saved.count++];
            entry->original[0] = 0;
            entry->orphan = 0;
        }
        const uint32_t *id = playlist_id(list);
        entry->id[0] = id[0];
        entry->id[1] = id[1];
        if (!entry->original[0])
            copy_name(entry->original, &library.length, SAVED_UNITS);
        copy_name(entry->renamed, &renamed->length, SAVED_UNITS);
    }
    cfw_preferences_write(&saved_path, &saved, sizeof(saved));
}

/* OSOS_PLAYLIST_GET_NAME entry: show the saved new name for this playlist. */
PATCH_ARM int cfw_playlist_get_name(void *list, struct native_name *name)
{
    int result = cfw_playlist_get_name_original(list, name);
    if (list)
        cfw_playlist_seen(list);
    struct saved_name *entry = list && name ? saved_for(list, &name->length) : NULL;
    if (entry)
        copy_name(&name->length, entry->renamed, SAVED_UNITS);
    return result;
}

static void get_name(void *list, struct native_name *name)
{
    typedef int (*get)(void *, struct native_name *);
    name->length = 0;
    /* Through the hooked entry, so this is the name the user sees. */
    ((get)OSOS_PLAYLIST_GET_NAME)(list, name);
}

/* Row highlighted when the Playlists hold menu was built; -1 when none. */
static int menu_row = -1;
/* Playlist chosen for renaming; NULL when idle. */
static void *rename_list;
/* Set by the Rename action until the Search controller it opens is built. */
static int rename_pending;
/* The Search controller doing the rename; other Search screens behave normally. */
static void *rename_controller;
static char typed[NAME_MAX + 1];

static void rename_reset(void)
{
    rename_list = NULL;
    rename_pending = 0;
    rename_controller = NULL;
}

static void *search_model(void)
{
    typedef void *(*lookup)(uint32_t);
    return ((lookup)OSOS_SEARCH_MODEL_LOOKUP)(OSOS_SEARCH_MODEL_ID);
}

static int set_query(void *model, const char *text, uint32_t length)
{
    typedef int (*set)(void *, uint32_t, uint32_t, const char *, uint32_t);
    return ((set)OSOS_MODEL_RESOURCE_SET)(model, STR_TYPE, OSOS_SEARCH_QUERY_ID, text,
                                    length);
}

static const char *get_query(void *model)
{
    typedef const char *(*get)(void *, uint32_t, uint32_t);
    return ((get)OSOS_MODEL_RESOURCE_GET)(model, STR_TYPE, OSOS_SEARCH_QUERY_ID);
}

static void *playlist_at(int row)
{
    typedef void *(*instance)(void);
    typedef void *(*row_item)(struct playlist_ref *, void *, int);
    /* Field-wise init: the payload links without libc, so no {0} (memset). */
    struct playlist_ref ref;
    ref.vtable = NULL;
    ref.list = NULL;
    if (row < 0)
        return NULL;
    ((row_item)OSOS_PLAYLISTS_ROW_ITEM)(&ref, ((instance)OSOS_PLAYLISTS_GET)(), row);
    return ref.list;
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

static uint32_t title_case(const char *source, uint32_t length)
{
    uint32_t n = 0;
    for (; n < length && n < NAME_MAX && source[n]; n++) {
        char c = source[n];
        if (c >= 'a' && c <= 'z' && (n == 0 || typed[n - 1] == ' '))
            c -= 'a' - 'A';
        typed[n] = c;
    }
    typed[n] = 0;
    return n;
}

static int rename_to(void *list, const char *text)
{
    typedef int (*set_name)(void *, struct native_name *);
    struct native_name name;
    uint16_t n = 0;
    /* The Search keyboard only produces ASCII letters, digits and spaces. Title-case
       here too: the first keystroke can reach the query without our hook. */
    for (; n < NAME_MAX && text[n]; n++) {
        char c = text[n];
        if (c >= 'a' && c <= 'z' && (n == 0 || text[n - 1] == ' '))
            c -= 'a' - 'A';
        name.text[n] = (uint8_t)c;
    }
    while (n && name.text[n - 1] == ' ')
        n--;
    if (!n)
        return -1;
    name.length = n;
    remember(list, &name);
    return ((set_name)OSOS_PLAYLIST_SET_NAME)(list, &name);
}

/* TSilverMediaListCntlr_Playlists vtable +0x148: remember the held row. */
PATCH_ARM int cfw_playlists_menu(void *controller, int row)
{
    typedef int (*build)(void *, int);
    menu_row = row;
    return ((build)OSOS_PLAYLISTS_BUILD_MENU)(controller, row);
}

/* Replaces the Cancel append in the Playlists hold menu: Rename, then Cancel. */
PATCH_ARM void cfw_playlists_menu_cancel(struct osos_context_menu *menu,
                                         uint32_t label, const char *action,
                                         uint32_t flags)
{
    osos_context_menu_append(menu, CFW_RENAME_PLAYLIST, "CFW_RenamePlaylist", 1);
    osos_context_menu_append(menu, label, action, flags);
}

int cfw_playlist_rename_action(void *controller, const char *action,
                               uint32_t argument)
{
    (void)controller;
    if (!cfw_string_equal(action, "CFW_RenamePlaylist"))
        return 0;
    if (argument != OSOS_ACTION_SUPPORT_QUERY) {
        rename_reset();
        rename_list = playlist_at(menu_row);
        rename_pending = rename_list != NULL;
        void *model = rename_list ? search_model() : NULL;
        if (model) {
            /* Start from the playlist's current name (ASCII; others become '?'). */
            struct native_name current;
            char text[SAVED_UNITS + 1];
            uint16_t n = 0;
            get_name(rename_list, &current);
            for (; n < current.length && n < SAVED_UNITS; n++)
                text[n] = current.text[n] < 0x80 ? (char)current.text[n] : '?';
            text[n] = 0;
            set_query(model, text, n);
            /* TSearchCntlr (vtable +0xec) restores Search_Main_Screen_WithKeyboard
               when this "keyboard was showing" flag is set; the rename screen has
               only its own layout, so switching would abort. */
            ((volatile uint8_t *)model)[OSOS_SEARCH_MODEL_KEYBOARD_FLAG] = 0;
        }
    }
    /* The screen event then pushes Search with its keyboard. */
    return 1;
}

/* Search controller construction (the only native site): claim it for a pending
   rename; any other new Search screen ends a rename that was abandoned. */
PATCH_ARM void *cfw_search_construct(void *controller, void *arg)
{
    typedef void *(*construct)(void *, void *);
    controller = ((construct)OSOS_SEARCH_CONSTRUCT)(controller, arg);
    if (rename_pending) {
        rename_pending = 0;
        rename_controller = controller;
    } else {
        rename_reset();
    }
    return controller;
}

/* Keystroke handler's query update: title-case the name while renaming. */
PATCH_ARM int cfw_search_set_query(void *model, uint32_t type, uint32_t id,
                                   const char *text, uint32_t length)
{
    typedef int (*set)(void *, uint32_t, uint32_t, const char *, uint32_t);
    if (rename_controller && type == STR_TYPE && id == OSOS_SEARCH_QUERY_ID && text)
        return ((set)OSOS_MODEL_RESOURCE_SET)(model, type, id, typed,
                                        title_case(text, length));
    return ((set)OSOS_MODEL_RESOURCE_SET)(model, type, id, text, length);
}

/* TSearchCntlr vtable +0x150 (HandleMenuOnKeyboard). */
PATCH_ARM int cfw_search_menu_on_keyboard(void *controller, uint32_t argument)
{
    typedef int (*handler)(void *, uint32_t);
    if (!rename_controller || controller != rename_controller)
        return ((handler)OSOS_SEARCH_MENU_ON_KEYBOARD)(controller, argument);
    void *list = rename_list;
    rename_reset();
    void *model = search_model();
    const char *text = model ? get_query(model) : NULL;
    if (text && *text)
        rename_to(list, text);
    if (model)
        set_query(model, "", 0);
    /* playlist_rename.ui binds controller.CFW_RenameDone to PopTopScreen. */
    emit(controller, "CFW_RenameDone");
    return 1;
}
