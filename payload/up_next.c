/* SPDX-License-Identifier: GPL-3.0-only */
/* Queue-backed native Songs screen. PlaybackList_GetEntryByIndex preserves shuffle order.
 * Player notifications keep its rows and the optional main-menu entry current. */
#include "osos.h"
#include "cfw.h"
#include "patch.h"
#include "play_next_ui.h"
#include "file_info_ui.h"
#include "up_next_ui.h"
#include "play_next.h"
#include "up_next.h"
#include "media_browser.h"
#include "ui_visibility.h"

#define FIELD(type, object, offset) (*(type *)((uint8_t *)(object) + (offset)))

enum {
    BROWSER_SONGS = OSOS_BROWSER_SONGS_OFFSET,
    BROWSER_SONGS_JUMP = OSOS_BROWSER_SONGS_JUMP_OFFSET,
    BROWSER_SONGS_COUNT = OSOS_BROWSER_SONGS_COUNT_OFFSET,
};

uint32_t cfw_build_songs_original(void *browser);
int cfw_song_menu_original(void *controller, int row);
int cfw_play_row_original(void *controller, int row, int a2, int a3);
void cfw_pop_top_original(uint32_t transition);
int cfw_np_event_original(void *model, void *event);

static struct {
    void *browser;         /* the Up Next browser while its model exists */
    void *model_reference; /* non-owning control block of that model (reused) */
    void *model;           /* that model, to tell the list its rows changed */
    struct osos_media_entry **rows; /* playback entries behind the rows (same order) */
    unsigned count;
    unsigned shown; /* last count written to the main menu */
    unsigned top;   /* 1 when row 0 is the playing song (not part of the queue) */
    int stale;      /* queue changed while Up Next was covered/away */
    int jumping;    /* a row was selected: Apple opens Now Playing next */
    void *screen;   /* its screen as Apple's navigator holds it */
    void *list_view;
} up_next = {.shown = ~0u};

static void *main_view; /* the main menu's list (seen on its Up Next row actions) */

static uint32_t top_screen(void)
{
    return osos_screen_id(osos_navigator_top());
}

static int showing_up_next(void)
{
    void *selection = osos_selection_get();
    return selection &&
           osos_model_ref_get(osos_selection_reference(selection)) == up_next.model;
}

/* True only while the Up Next list is the screen on top (not covered by Now Playing, not the
   main menu). Reloads and highlight changes must never touch other screens. */
static int up_next_on_screen(void)
{
    return top_screen() == OSOS_SONGS_SCREEN && showing_up_next() &&
           (!up_next.list_view || osos_ui_selection() == up_next.list_view);
}

/* While Up Next is on top, remember which navigator screen it is (to know when Menu on Now
   Playing uncovers it again). */
static void note_screen(void)
{
    if (top_screen() == OSOS_SONGS_SCREEN && showing_up_next())
        up_next.screen = osos_navigator_top();
}

/* Stopping playback drops the current track and its playback list. */
static int playback_dropped(void)
{
    struct osos_player *player = osos_player_get();
    return !player || !player->current;
}

/* Now Playing can share the Up Next model while covering its list. */
static int up_next_on_top(void)
{
    uint32_t screen = top_screen();
    return (screen == OSOS_SONGS_SCREEN || screen == OSOS_NOW_PLAYING_SCREEN) &&
           showing_up_next();
}

/* Leave Up Next for the main menu (Apple's navigator.PopToMainScreen). Nothing may be posted
   to the Up Next controller after this. */
static void leave_up_next(void)
{
    up_next.screen = NULL;
    osos_pop_to_main_screen();
}

/* Songs after the current one. Read without the player lock (word reads only); callers that
   act on entries take the lock themselves. */
static unsigned queued(struct osos_player *player)
{
    if (!player || !player->current || player->current_index < 0 ||
        !osos_playback_list_valid(&player->playback))
        return 0;
    struct osos_media_entry *root = osos_list_root(player->playback.list);
    if (!root || root->groups || root->tracks == UINT16_MAX)
        return 0;
    unsigned tracks = root->tracks, next = (unsigned)player->current_index + 1;
    return tracks > next ? tracks - next : 0;
}

static void write_message(unsigned count)
{
    char text[40];
    unsigned n = 0;
    char digits[11];
    unsigned d = 0;
    unsigned value = count;
    do {
        digits[d++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (d)
        text[n++] = digits[--d];
    const char *unit = count == 1 ? " Song" : " Songs";
    while (*unit)
        text[n++] = *unit++;
    text[n++] = 0;
    osos_resource_set(osos_resource_bank(), 0x53747220, CFW_UpNext_Message, text, n);
}

static int queue_changed;
static uint32_t queue_generation, menu_generation;
static void *settings_model; /* captured from the Settings property getter */

static void publish_queue_count(unsigned count)
{
    write_message(count);
    if (settings_model)
        osos_model_notify(settings_model, OSOS_NOTIFY_ALL, CFW_UpNext_Count);
    cfw_ui_visibility_set_available(CFW_UpNext_MainItem, count != 0);
    osos_menu_item_changed(CFW_UpNext_MainItem);
}

/* Settings-model property behind the preview's "n Songs" line. */
int cfw_up_next_property(void *model, uint32_t type, uint32_t property, uintptr_t *out)
{
    settings_model = model;
    if (property != CFW_UpNext_Count || type != OSOS_PROPERTY_STRING)
        return 0;
    *out = osos_ui_string(CFW_UpNext_Message);
    return 1;
}

/* Defer highlight restoration until the list finishes its row-count layout. */
static void post_action(void *controller, const char *name)
{
    struct osos_string action;
    osos_string_init(&action, name);
    osos_controller_post_action(controller, &action);
    osos_string_destroy(&action);
}

enum { RELOAD_KEEP = 0x7fffffff };

/* Move the on-screen highlight (the list view's row; OSOS_SELECTION_SET_ROW only records it). */
static void set_highlight(void *selection, int row)
{
    if (selection)
        osos_selection_set_row(selection, row);
    void *view = osos_ui_selection();
    if (view)
        osos_list_view_set_row(view, row);
}

static int highlight_row(void)
{
    void *selection = osos_selection_get();
    return selection ? FIELD(int, selection, 0x2c) : 0;
}

static void forget_rows(void)
{
    up_next.rows = NULL; /* owned by the browser's song vector */
    up_next.count = 0;
}

/* The queue changed: drop the cached rows and tell the open list to re-read them, so it never
   shows (or lets you act on) songs that already played or were removed. */
static void reload_list(int highlight, int blink)
{
    if (!up_next.browser || !up_next.model)
        return;
    /* Close the queue screen before reading a playback list that has been dropped. */
    if (playback_dropped() && up_next_on_top()) {
        osos_browser_clear_songs(up_next.browser);
        forget_rows();
        leave_up_next();
        return;
    }
    /* Drop the rows so the list re-reads the live queue when it is drawn again (e.g. back from
       Now Playing after skipping back). */
    osos_browser_clear_songs(up_next.browser);
    forget_rows();
    /* Native playlist edits re-announce the selection title and row count (0x0822f59c).
       Reload only our focused list: this API otherwise resets another screen's selection. */
    void *selection = osos_selection_get();
    void *shown =
        selection ? osos_model_ref_get(osos_selection_reference(selection)) : NULL;
    if (shown != up_next.model) {
        up_next.stale =
            1; /* e.g. Now Playing on top: refresh on the next Up Next action */
        return;
    }
    /* blink: an edit made from Up Next's own hold menu, so Up Next is the list underneath. */
    if (!blink && !up_next_on_screen()) { /* blink 2: uncovered again, forced */
        up_next.stale = 1;
        return;
    }
    up_next.stale = 0;
    /* Reloading puts the highlight back on the first row: keep it where the user was. */
    int previous = FIELD(int, selection, 0x2c);
    osos_selection_reload(selection);
    unsigned count = queued(osos_player_get());
    count += count ? 1 : 0; /* the playing song's row on top */
    if (highlight == RELOAD_KEEP)
        highlight = previous;
    if (!count) {
        /* Closing the empty queue invalidates its controller; do not post more actions. */
        if (blink || !up_next.jumping)
            leave_up_next(); /* (only ever while Up Next is the screen on top) */
        return;
    }
    if (highlight < 0)
        highlight = 0;
    if ((unsigned)highlight >= count)
        highlight = (int)count - 1;
    set_highlight(selection, highlight);
    if (blink == 1)
        osos_row_blink(highlight);
}

void cfw_up_next_refresh(void)
{
    unsigned count = queued(osos_player_get());
    if (count == up_next.shown && !queue_changed)
        return;
    queue_changed = 0;
    queue_generation++;
    /* Songs drop off the top as playback moves on: keep the highlight on the same song. */
    int highlight = RELOAD_KEEP;
    if (up_next.shown != ~0u && count < up_next.shown)
        highlight = highlight_row() - (int)(up_next.shown - count);
    up_next.shown = count;
    reload_list(highlight, 0);
    publish_queue_count(count);
}

/* Now Playing receives track/order changes from every screen.
   Timer-only events must not invalidate the song cache. */
PATCH_ARM int cfw_up_next_np_event(void *model, void *event)
{
    static int inside;
    uint32_t code = FIELD(uint32_t, event, 4);
    if (code == 0x80009 || code == 0x8000a || code == 0x8000f || code == 0x80010)
        queue_changed = 1;
    int result = cfw_np_event_original(model, event);
    if (!inside) { /* a refresh may raise player events itself */
        inside = 1;
        cfw_up_next_refresh();
        inside = 0;
    }
    return result;
}

/* Replaces Apple's tail call MenuItem_SetVisible(Now Playing, r1); flags are lost, so r1 is
   reduced like "movlo r1, #0" did (r1 = 1 - player-idle, 1 only when playing). */
PATCH_ARM int cfw_up_next_now_playing_visible(uint32_t item, uint32_t visible)
{
    int result = osos_menu_item_set_visible(item, visible == 1);
    cfw_up_next_refresh();
    return result;
}

PATCH_ARM void cfw_up_next_next(void)
{
    osos_player_skip(0);
    cfw_up_next_refresh();
}

PATCH_ARM void cfw_up_next_previous(void)
{
    osos_player_skip(1);
    cfw_up_next_refresh();
}

/* Refresh Up Next when Menu uncovers it. */
PATCH_ARM void cfw_up_next_pop_top(uint32_t transition)
{
    cfw_pop_top_original(transition);
    /* Now Playing shares this navigator screen, but its hidden list ignores reloads.
       Refresh synchronously on return; posted actions are not delivered here. */
    if (up_next.screen && osos_navigator_top() == up_next.screen) {
        if (playback_dropped() || !queued(osos_player_get()))
            leave_up_next();
        else
            reload_list(RELOAD_KEEP, 2);
    }
}

/* ---- the list --------------------------------------------------------------------------- */

PATCH_ARM uint32_t cfw_up_next_build_songs(void *browser)
{
    if (!browser || browser != up_next.browser)
        return cfw_build_songs_original(browser);
    note_screen(); /* drawing its rows: Up Next is on top */
    if (FIELD(void *, browser, BROWSER_SONGS))
        return 0;
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    unsigned count = queued(player), first = (unsigned)player->current_index + 1,
             used = 0;
    struct osos_media_entry **rows = osos_songs_alloc((count + 1) * sizeof(*rows));
    up_next.top = 0;
    if (rows) {
        /* Row 0: the song playing now (selecting it just shows Now Playing). */
        struct osos_media_entry *playing =
            count ? osos_playback_entry(&player->playback, (int)first - 1) : NULL;
        if (playing && playing->song.record) {
            rows[used++] = playing;
            up_next.top = 1;
        }
        for (unsigned i = 0; i < count; i++) {
            struct osos_media_entry *entry =
                osos_playback_entry(&player->playback, first + i);
            if (entry && entry->song.record)
                rows[used++] = entry;
        }
    }
    osos_player_unlock(player);
    if (!rows)
        return 0;
    FIELD(void *, browser, BROWSER_SONGS) = rows;
    FIELD(void *, browser, BROWSER_SONGS_JUMP) = NULL;
    FIELD(uint32_t, browser, BROWSER_SONGS_COUNT) = used;
    up_next.rows = rows;
    up_next.count = used;
    return 0;
}

/* Native contextual navigation clears the browser cache before reading the item.
 * Its getter only reads cached rows, so rebuild the queue-backed vector first. */
PATCH_ARM void cfw_up_next_song_item(struct osos_media_item *out, void *model, int row)
{
    if (model == up_next.model && up_next.browser)
        cfw_up_next_build_songs(up_next.browser);
    osos_library_read_song_item(out, model, row);
}

int cfw_up_next_owns_model(void *model)
{
    return model && model == up_next.model;
}

/* Called from the model destructor before Apple unregisters and frees the browser. */
void cfw_up_next_model_destroyed(void *model)
{
    if (cfw_up_next_owns_model(model)) {
        osos_browser_clear_songs(up_next.browser);
        up_next.browser = NULL;
        up_next.model_reference = NULL;
        up_next.model = NULL;
        up_next.screen = up_next.list_view = NULL;
        forget_rows();
    }
}

static void *create_model(void **reference)
{
    /* The selection keeps the last model alive after its screen closes: reuse it. */
    if (up_next.model_reference && up_next.browser) {
        osos_model_ref_assign(reference, &up_next.model_reference);
        void *model = osos_model_ref_get(reference);
        if (model && FIELD(void *, model, 0x40) == up_next.browser) {
            up_next.model = model;
            return model;
        }
        osos_model_ref_release(reference);
        *reference = NULL;
    }
    void *model = cfw_media_browser_create(reference);
    if (!model)
        return NULL;
    up_next.model_reference = *reference;
    up_next.model = model;
    return model;
}

static void open_up_next(void *controller)
{
    if (!queued(osos_player_get()))
        return;
    void *reference = NULL;
    void *model = create_model(&reference);
    if (!model)
        return;
    osos_library_model_prepare_songs(model);
    up_next.browser = FIELD(void *, model, 0x40);
    osos_browser_clear_songs(up_next.browser); /* rebuild from the queue */
    forget_rows();
    void *selection = osos_selection_get();
    osos_selection_set_title(selection, 0x80, CFW_UpNext_Title);
    osos_selection_set_row(selection, 0); /* open at the top */
    up_next.screen = up_next.list_view = NULL;
    up_next.stale = 0;
    void *screen = osos_create_screen_controller(
        osos_controller_registry(), OSOS_SONGS_SCREEN, OSOS_SONGS_LAYOUT, 2);
    if (screen) {
        osos_model_ref_assign((uint8_t *)screen + 0xb4, &reference);
        FIELD(uint8_t, screen, 0xb2) = 0;
        osos_controller_push(controller, screen);
        note_screen();
    }
    osos_model_ref_release(&reference);
}

/* Main / Music menu actions (called first from cfw_album_artists_action). */
int cfw_up_next_action(void *controller, const char *action, uint32_t argument)
{
    int open = cfw_string_equal(action, "CFW_UpNext");
    if (!open && !cfw_string_equal(action, "CFW_UpNextHilited"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    main_view = osos_ui_selection();
    if (open)
        open_up_next(controller);
    else {
        up_next.shown = ~0u;
        cfw_up_next_refresh();
    }
    return 1;
}

/* ---- the hold-Select menu and its actions ------------------------------------------------ */

static int is_up_next(void *controller)
{
    if (!controller || !up_next.browser)
        return 0;
    void *model = osos_song_controller_model(controller);
    return model && FIELD(void *, model, 0x40) == up_next.browser;
}

PATCH_ARM int cfw_up_next_song_menu(void *controller, int row)
{
    if (!is_up_next(controller))
        return cfw_song_menu_original(controller, row);
    if (playback_dropped()) {
        leave_up_next(); /* back on Up Next after playback stopped: nothing left */
        return 1;
    }
    up_next.list_view = osos_ui_selection();
    note_screen();
    if (up_next.stale) {
        reload_list(0, 0); /* the menu would act on old rows: refresh, user re-picks */
        return 1;
    }
    uint32_t menu[8];
    struct osos_context_menu *m = (struct osos_context_menu *)menu;
    osos_context_menu_init(m);
    /* The row the menu was opened for (Apple stores the selection index; with the reused Up
       Next model that index can lag behind, so prefer the row the list passed in). */
    int selected = row;
    if (selected < 0) {
        void *selection = osos_ui_selection();
        selected = osos_list_view_row(selection);
    }
    int playing = up_next.top && selected == 0;
    if (!playing) {
        osos_context_menu_append(m, CFW_UPNEXT_REMOVE, "CFW_RemoveFromQueue", 1);
        osos_context_menu_append(m, CFW_PLAY_NEXT, "CFW_PlayNext", 1);
        osos_context_menu_append(m, CFW_PLAY_LAST, "CFW_PlayLast", 1);
    }
    osos_context_menu_append(m, CFW_UPNEXT_CLEAR, "CFW_ClearUpNext", 1);
    osos_context_menu_append(m, CFW_UPNEXT_ALBUM, "HandleViewAlbum", 1);
    osos_context_menu_append(m, CFW_UPNEXT_ARTIST, "HandleViewArtist", 1);
    osos_context_menu_append(m, CFW_FILE_INFO_LABEL, "CFW_FileInfo", 1);
    osos_context_menu_append(m, OSOS_CANCEL_LABEL, "HandleCancel", 0);
    FIELD(int, controller, 0xd4) = selected;
    menu_generation = queue_generation;
    if (selected != -1)
        osos_context_menu_show(controller, m);
    osos_context_menu_destroy(m);
    return 1;
}

/* Remove one queued entry (checked to still be queued); the player lock must be held. */
static int delete_entry(struct osos_player *player, struct osos_media_entry *entry)
{
    if (!entry)
        return 0;
    struct osos_media_list *list = player->playback.list;
    unsigned count = queued(player), first = (unsigned)player->current_index + 1;
    int found = 0;
    for (unsigned i = 0; i < count && !found; i++)
        found = osos_playback_entry(&player->playback, first + i) == entry;
    if (!found)
        return 0;
    osos_database_begin_update(osos_list_database(list));
    int result = osos_media_entry_delete(entry);
    osos_database_end_update(osos_list_database(list));
    return result == 0;
}

static int remove_row(struct osos_player *player, int row)
{
    if (row < 0 || (unsigned)row >= up_next.count || !up_next.rows)
        return 0;
    struct osos_media_entry *entry = osos_playback_entry(
        &player->playback, player->current_index + 1 + row - (int)up_next.top);
    return delete_entry(player, entry);
}

/* Drop every song queued behind the current one, leaving an empty queue. The player lock must
   be held. The song at current index + 1 is deleted repeatedly: each delete shifts the songs
   after it down, so this drains the queue. A failed delete stops early; the return says
   whether anything went. */
static int clear_queued(struct osos_player *player)
{
    int first = player->current_index + 1, changed = 0;
    while (queued(player)) {
        if (!delete_entry(player, osos_playback_entry(&player->playback, first)))
            break;
        changed = 1;
    }
    return changed;
}

/* Selecting a queued song removes intervening entries, then starts its playback-list index.
   The native Next action honors Repeat One and can advance chapters instead of songs. */
static void jump_to_row(int row)
{
    if (row < (int)up_next.top || (unsigned)row >= up_next.count || !up_next.rows)
        return; /* the playing song: the list just shows Now Playing */
    up_next.jumping = 1;
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    unsigned first = (unsigned)player->current_index + 1;
    struct osos_media_entry *target =
        osos_playback_entry(&player->playback, first + row - up_next.top);
    int changed = 0;
    if (row > (int)up_next.top) {
        cfw_queue_edit_begin(player);
        for (int i = (int)up_next.top; i < row; i++) {
            if (!delete_entry(player, osos_playback_entry(&player->playback, first)))
                break;
            changed = 1;
        }
        cfw_queue_edit_end(player, changed);
    }
    int ready = target && queued(player) &&
                osos_playback_entry(&player->playback, first) == target;
    osos_player_unlock(player);
    forget_rows();
    if (up_next.browser)
        osos_browser_clear_songs(up_next.browser);
    if (ready)
        osos_player_play_index(player, (int)first);
    up_next.shown = ~0u;
    cfw_up_next_refresh();
    up_next.jumping = 0;
}

PATCH_ARM int cfw_up_next_play_row(void *controller, int row, int a2, int a3)
{
    if (!is_up_next(controller))
        return cfw_play_row_original(controller, row, a2, a3);
    return 1; /* never play an Up Next row as a new list */
}

static int pending_highlight = -1;

/* Song-list actions on the Up Next screen; called first from cfw_song_action(). */
int cfw_up_next_song_action(void *controller, const char *action, uint32_t argument)
{
    /* Play/Pause on a highlighted row would play that row as a new list: on Up Next it just
       pauses / resumes, like the Play/Pause button anywhere else. */
    /* Closing controllers still receive actions; never reload the newly focused main menu. */
    /* Hold-menu actions arrive while the menu is the focused list: the Up Next list was already
       seen when the menu opened, and the menu refused to open on stale rows. */
    void *focused = osos_ui_selection();
    int menu_action = cfw_string_equal(action, "CFW_RemoveFromQueue") ||
                      cfw_string_equal(action, "CFW_ClearUpNext") ||
                      cfw_string_equal(action, "CFW_PlayNext") ||
                      cfw_string_equal(action, "CFW_PlayLast") ||
                      cfw_string_equal(action, "CFW_FileInfo") ||
                      cfw_string_equal(action, "HandleViewAlbum") ||
                      cfw_string_equal(action, "HandleViewArtist") ||
                      cfw_string_equal(action, "HandleCancel");
    if (is_up_next(controller) && argument != OSOS_ACTION_SUPPORT_QUERY &&
        menu_action && menu_generation != queue_generation &&
        !cfw_string_equal(action, "HandleCancel")) {
        osos_controller_restore_layout(controller, OSOS_SONG_LAYOUT_OFFSET);
        reload_list(RELOAD_KEEP, 0);
        return 1;
    }
    if (is_up_next(controller) && argument != OSOS_ACTION_SUPPORT_QUERY &&
        focused != main_view && !menu_action) {
        if (playback_dropped()) {
            leave_up_next(); /* back on Up Next after playback stopped: nothing left */
            return 1;
        }
        note_screen();
        if (up_next.stale) {
            /* Back on Up Next after the queue moved on: show it fresh. A select would act on the
               old rows, so it is used up by the refresh (pick again). */
            reload_list(0, 0);
            if (cfw_string_equal(action, "HandleSelect"))
                return 1;
        }
    }
    if (cfw_string_equal(action, "HandleSelect") && is_up_next(controller)) {
        if (argument != OSOS_ACTION_SUPPORT_QUERY) {
            void *selection = osos_ui_selection();
            int row = osos_list_view_row(selection);
            if (row != -1)
                row = osos_song_controller_map_row(controller, row);
            jump_to_row(row); /* the list itself then shows Now Playing */
        }
        return 1;
    }
    if (cfw_string_equal(action, "CFW_UpNextHighlight") && is_up_next(controller)) {
        if (argument != OSOS_ACTION_SUPPORT_QUERY && pending_highlight >= 0 &&
            up_next_on_screen()) {
            void *selection = osos_selection_get();
            unsigned count = up_next.count;
            int row = pending_highlight;
            pending_highlight = -1;
            if (selection && count) {
                if ((unsigned)row >= count)
                    row = (int)count - 1;
                set_highlight(selection, row);
            }
        }
        return 1;
    }
    if (cfw_string_equal(action, "HandlePlayPause") && is_up_next(controller)) {
        if (argument != OSOS_ACTION_SUPPORT_QUERY)
            osos_player_play_pause();
        return 1;
    }
    int remove = cfw_string_equal(action, "CFW_RemoveFromQueue");
    int clear = cfw_string_equal(action, "CFW_ClearUpNext");
    int next = cfw_string_equal(action, "CFW_PlayNext");
    int last = cfw_string_equal(action, "CFW_PlayLast");
    if (!remove && !clear && !next && !last)
        return 0;
    if (!is_up_next(controller))
        return 0; /* Play Next elsewhere: osos-lab's own handler */
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    if (up_next.stale || !up_next.rows) {
        reload_list(RELOAD_KEEP, 1);
        return 1;
    }
    int row = FIELD(int, controller, 0xd4);
    if (row < 0 || (unsigned)row >= up_next.count)
        return 1;
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    int top = (int)up_next.top;
    int changed = 0;
    if (clear) { /* every song still queued behind the current one */
        cfw_queue_edit_begin(player);
        changed = clear_queued(player);
        cfw_queue_edit_end(player, changed);
    } else if (row < top) {
        /* the playing song: nothing to move or remove */
    } else if ((next && row > top && (unsigned)row < up_next.count) ||
               (last && (unsigned)row + 1 < up_next.count)) {
        /* Move: insert a copy right after the current song (Play Next) or at the end of the
           queue (Play Last), then drop the old position. */
        struct osos_media_item item;
        struct osos_media_entry *old = osos_playback_entry(
            &player->playback, player->current_index + 1 + row - top);
        osos_media_item_init_record(&item, old ? old->song.record : NULL);
        if (osos_media_item_valid(&item)) {
            cfw_queue_edit_begin(player);
            int inserted = cfw_play_next_insert(player, &item, last);
            if (inserted)
                changed = delete_entry(player, old);
            cfw_queue_edit_end(player, inserted);
        }
    } else if (remove) {
        cfw_queue_edit_begin(player);
        changed = remove_row(player, row);
        cfw_queue_edit_end(player, changed);
    }
    osos_player_unlock(player);
    unsigned count = queued(player);
    /* Highlight: after a remove, the song above it; after a move, where the song went; after a
       clear, the top row, which only matters if a delete failed and rows were kept. Then the
       double blink Apple uses to confirm Add to On-The-Go. */
    int highlight = !changed ? row
                    : clear  ? 0
                    : remove ? row - 1
                    : next   ? (int)up_next.top
                             : (int)(count + up_next.top) - 1;
    up_next.shown = count; /* reload once, here, with this highlight */
    reload_list(highlight, changed ? 1 : 2);
    if (remove && changed &&
        count) { /* count 0: Up Next was popped, its controller is gone */
        pending_highlight = highlight < 0 ? 0 : highlight;
        post_action(controller, "CFW_UpNextHighlight");
    }
    publish_queue_count(count);
    return 1;
}
