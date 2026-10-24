/* SPDX-License-Identifier: GPL-3.0-only */
/* Up Next: the play queue as a song list.
 *
 * The queue is the player's playback list after the current song: osos-lab's Play Next / Play
 * Last insert copies there, and the player walks it in playback order (shuffle-aware through
 * PlaybackList_GetEntryByIndex).
 *
 *  - Main menu: a row "Up Next" right below Now Playing (up_next.ui), shown only
 *    while songs are queued. Apple decides Now Playing's visibility in NowPlayingModel
 *    (tail call MenuItem_SetVisible(MainMenu_List_NowPlaying, visible) at 0x081ef1d4); that
 *    call is redirected here so Up Next follows every player change.
 *  - Preview: the Compilations "No Compilations" watermark layout cloned with "n Songs".
 *  - List: Apple's own Songs screen (two-line rows, quick scroll, context menu) on a library
 *    browser of our own whose song vector (MediaBrowser_BuildSongs) is the queue.
 *  - Hold Select: Remove from Queue, Play Next, Go to Album, Go to Artist, Song Info, Cancel. */
#include "osos.h"
#include "cfw.h"
#include "patch.h"
#include "play_next_ui.h"
#include "song_info_ui.h"
#include "up_next_ui.h"
#include "play_next.h"
#include "up_next.h"

#define FIELD(type, object, offset) (*(type *)((uint8_t *)(object) + (offset)))

enum {
    NOW_PLAYING_VISIBLE_TAIL = 0x081ef1d0, /* movlo r1, #0 ; b MenuItem_SetVisible */
    MENU_ITEM_SET_VISIBLE = 0x08194ff0,
    BUILD_SONGS = 0x0807b858,              /* MediaBrowser_BuildSongs */
    SONG_MENU = 0x08219478,                /* SongList_BuildAndShowContextualMenu */
    SONGS_ALLOC = 0x08052324,              /* the allocator BuildSongs uses (Heap_Free frees) */
    MEDIA_ENTRY_DELETE = 0x0803db30,       /* unlink + free one list entry */
    SELECTION_GET = 0x08169178,
    SELECTION_SET_TITLE = 0x08169c30,
    SELECTION_MODEL = 0x083ccd1c,          /* model behind a selection (ref at selection+0x18) */
    SELECTION_RELOAD = 0x0816d2f8,         /* notify title ('Str ' 0x7801) and row count ('VMax') */
    SELECTION_SET_ROW = 0x0816d27c,        /* move the highlight (index kept at selection+0x2c) */
    ROW_BLINK = 0x082896f4,                /* Apple's double blink of a row (Add to On-The-Go) */
    POP_TO_MAIN_SCREEN = 0x08289714,       /* navigator.PopToMainScreen */
    NAVIGATOR = 0x08104d00,                /* the screen navigator (PopToScreen's) */
    POP_TOP_SCREEN = 0x082896b8,           /* the navigator pop behind the Menu button */
    NOW_PLAYING_EVENT = 0x081efe74,        /* NowPlayingModel's player-event handler */
    LIST_VIEW_SET_ROW = 0x184,             /* on OSOS_UI_SELECTION's view: move the highlight */
    PLAYER_NEXT = 0x081c39b8,              /* Apple's "HandleNext" (the Next button) */
    PLAYER_PLAY_PAUSE = 0x081c50fc,        /* Apple's "HandlePlayPause" (the Play/Pause button) */
    PLAYER_PREVIOUS = 0x081c4f68,          /* Apple's "HandlePrevious" (the Previous button) */
    PLAYER_GET = 0x0824ff20,               /* the player interface the button handlers use */
    CONTEXT_MENU_INIT = 0x081624e8,
    CONTEXT_MENU_DESTROY = 0x0816250c,
    CONTEXT_MENU_SHOW = 0x08129d78,
    PLAY_SONG_ROW = 0x0820fc54,            /* MediaListController_PlaySongRow */
    SONGS_SCREEN = 0x0dad07e9,             /* MediaLists_Songs_Screen */
    NOW_PLAYING_SCREEN = 0x0dad096c,       /* NowPlaying_Screen */
    SONGS_LAYOUT = 0x0dad07eb,             /* MediaLists_Songs_Screen_WithArtist */
    STR_CANCEL = 0x0dad011d,
    BROWSER_SONGS = 0xeec, BROWSER_SONGS_JUMP = 0xef0, BROWSER_SONGS_COUNT = 0xef4,
};

uint32_t cfw_build_songs_original(void *browser);
int cfw_song_menu_original(void *controller, int row);
int cfw_play_row_original(void *controller, int row, int a2, int a3);
void cfw_pop_top_original(uint32_t transition);
int cfw_np_event_original(void *model, void *event);

static struct {
    void *browser;                  /* the Up Next browser while its model exists */
    void *model_reference;          /* non-owning control block of that model (reused) */
    void *model;                    /* that model, to tell the list its rows changed */
    struct osos_media_entry **rows; /* playback entries behind the rows (same order) */
    unsigned count;
    unsigned shown;                 /* last count written to the main menu */
    unsigned top;                   /* 1 when row 0 is the playing song (not part of the queue) */
    void *controller;               /* its song controller, while its screen exists */
    int stale;                      /* queue changed while Up Next was covered/away */
    int jumping;                    /* a row was selected: Apple opens Now Playing next */
    void *screen;                   /* its screen as Apple's navigator holds it */
} up_next = {0, 0, 0, 0, 0, ~0u, 0, 0, 0, 0, 0};

static void *main_view;             /* the main menu's list (seen on its Up Next row actions) */

/* The screen on top of Apple's navigator (as navigator.PopToScreen checks it: top screen vtbl
   0x68 = its screen id), and whether the music selection is showing the Up Next model. */
static void *navigator_top(void)
{
    void *navigator = ((void *(*)(void))NAVIGATOR)();
    return navigator ? ((void *(*)(void *))osos_method(navigator, 0x14))(navigator) : NULL;
}

static uint32_t top_screen(void)
{
    void *top = navigator_top();
    return top ? ((uint32_t (*)(void *))osos_method(top, 0x68))(top) : 0;
}


static int showing_up_next(void)
{
    void *selection = ((void *(*)(void))SELECTION_GET)();
    return selection &&
           ((void *(*)(void *))SELECTION_MODEL)((uint8_t *)selection + 0x18) == up_next.model;
}

/* True only while the Up Next list is the screen on top (not covered by Now Playing, not the
   main menu). Reloads and highlight changes must never touch other screens. */
static int up_next_on_screen(void)
{
    return top_screen() == SONGS_SCREEN && showing_up_next();
}

/* While Up Next is on top, remember which navigator screen it is (to know when Menu on Now
   Playing uncovers it again). */
static void note_screen(void)
{
    if (top_screen() == SONGS_SCREEN && showing_up_next())
        up_next.screen = navigator_top();
}

/* Playback stopped (e.g. Previous on the first song): the player drops its current song and its
   whole playback list (current NULL, index -1, list NULL), so the queue no longer exists. */
static int playback_dropped(void)
{
    struct osos_player *player = osos_player_get();
    return !player || !player->current;
}

/* Up Next (or Now Playing opened from it) is the screen on top of Apple's navigator (as
   navigator.PopToScreen checks it: top screen vtbl 0x68 = its screen id), and the music
   selection is showing the Up Next model. */
static int up_next_on_top(void)
{
    uint32_t screen = top_screen();
    return (screen == SONGS_SCREEN || screen == NOW_PLAYING_SCREEN) && showing_up_next();
}

/* Leave Up Next for the main menu (Apple's navigator.PopToMainScreen). Nothing may be posted
   to the Up Next controller after this. */
static void leave_up_next(void)
{
    up_next.controller = up_next.screen = NULL;
    ((void (*)(void))POP_TO_MAIN_SCREEN)();
}

PATCH_JUMP(NOW_PLAYING_VISIBLE_TAIL, 0x33a01000, 0xea023c73, cfw_up_next_now_playing_visible);
PATCH_JUMP(BUILD_SONGS, 0xe92d4ff1, 0xe24dd018, cfw_up_next_build_songs);
PATCH_JUMP(SONG_MENU, 0xe92d4ff0, 0xe24dd034, cfw_up_next_song_menu);
PATCH_JUMP(PLAY_SONG_ROW, 0xe92d4ff0, 0xe1a06000, cfw_up_next_play_row);

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

static void *settings_model;        /* captured from the Settings property getter */

/* Settings-model property behind the preview's "n Songs" line. */
int cfw_up_next_property(void *model, uint32_t type, uint32_t property, uintptr_t *out)
{
    settings_model = model;
    if (property != CFW_UpNext_Count || type != OSOS_PROPERTY_STRING)
        return 0;
    *out = osos_ui_string(CFW_UpNext_Message);
    return 1;
}

/* Run an action on the controller after the current one finishes (Apple queues these; used for
   "GotoNowPlaying"). The list re-lays itself out after a row-count change, so the highlight is
   restored from a follow-up action rather than straight away. */
static void post_action(void *controller, const char *name)
{
    struct osos_string action;
    osos_string_init(&action, name);
    ((void (*)(void *, struct osos_string *))osos_method(controller, 0x11c))(controller, &action);
    osos_string_destroy(&action);
}

enum { RELOAD_KEEP = 0x7fffffff };
/* Move the on-screen highlight (the list view's row; SELECTION_SET_ROW only records it). */
static void set_highlight(void *selection, int row)
{
    if (selection)
        ((void (*)(void *, int))SELECTION_SET_ROW)(selection, row);
    void *view = ((void *(*)(void))OSOS_UI_SELECTION)();
    if (view)
        ((void (*)(void *, int))osos_method(view, LIST_VIEW_SET_ROW))(view, row);
}

static int highlight_row(void)
{
    void *selection = ((void *(*)(void))SELECTION_GET)();
    return selection ? FIELD(int, selection, 0x2c) : 0;
}

/* The queue changed: drop the cached rows and tell the open list to re-read them, so it never
   shows (or lets you act on) songs that already played or were removed. */
static void reload_list(int highlight, int blink)
{
    if (!up_next.browser || !up_next.model)
        return;
    /* Queue gone while Up Next isn't on screen (e.g. USB connect stops playback while the
       library is shutting down; the row is hidden anyway): don't make the list re-read anything.
       Removing the last song while looking at Up Next still refreshes it. */
    if (playback_dropped() && up_next_on_top()) {
        ((void (*)(void *))OSOS_BROWSER_CLEAR_SONGS)(up_next.browser);
        up_next.rows = NULL;
        up_next.count = 0;
        leave_up_next();
        return;
    }
    if (!queued(osos_player_get()) && !blink && !up_next_on_screen()) {
        return;
    }
    /* Drop the rows so the list re-reads the live queue when it is drawn again (e.g. back from
       Now Playing after skipping back). */
    ((void (*)(void *))OSOS_BROWSER_CLEAR_SONGS)(up_next.browser);
    up_next.rows = NULL;
    up_next.count = 0;
    /* Tell the open list its rows changed the way Apple does after editing a playlist
       (0x0822f59c): the music selection re-announces its title and row count ('VMax'), and the
       list re-reads both. Only when the selection is showing the Up Next model, and only while
       Up Next itself is on screen (it resets whichever list is focused to the top); otherwise
       the next Up Next select or hold re-announces it. */
    void *selection = ((void *(*)(void))SELECTION_GET)();
    void *shown = selection ? ((void *(*)(void *))SELECTION_MODEL)((uint8_t *)selection + 0x18)
                            : NULL;
    if (shown != up_next.model) {
        up_next.stale = 1;          /* e.g. Now Playing on top: refresh on the next Up Next action */
        return;
    }
    /* blink: an edit made from Up Next's own hold menu, so Up Next is the list underneath. */
    if (!blink && !up_next_on_screen()) {   /* blink 2: uncovered again, forced */
        up_next.stale = 1;
        return;
    }
    up_next.stale = 0;
    /* Reloading puts the highlight back on the first row: keep it where the user was. */
    int previous = FIELD(int, selection, 0x2c);
    ((void (*)(void *))SELECTION_RELOAD)(selection);
    unsigned count = queued(osos_player_get());
    count += count ? 1 : 0;         /* the playing song's row on top */
    if (highlight == RELOAD_KEEP)
        highlight = previous;
    if (!count) {
        /* Queue emptied from Up Next's hold menu: the list now knows it has no rows; go back to
           the main menu (Apple's navigator.PopToMainScreen). Nothing may be posted to the Up
           Next controller after. */
        if (blink || !up_next.jumping)
            leave_up_next();        /* (only ever while Up Next is the screen on top) */
        return;
    }
    if (highlight < 0)
        highlight = 0;
    if ((unsigned)highlight >= count)
        highlight = (int)count - 1;
    set_highlight(selection, highlight);
    if (blink == 1)
        ((void (*)(int))ROW_BLINK)(highlight);
}

void cfw_up_next_refresh(void)
{
    unsigned count = queued(osos_player_get());
    if (count == up_next.shown)
        return;
    /* Songs drop off the top as playback moves on: keep the highlight on the same song. */
    int highlight = RELOAD_KEEP;
    if (up_next.shown != ~0u && count < up_next.shown)
        highlight = highlight_row() - (int)(up_next.shown - count);
    up_next.shown = count;
    reload_list(highlight, 0);
    write_message(count);
    if (settings_model)
        osos_model_notify(settings_model, OSOS_NOTIFY_ALL, CFW_UpNext_Count);
    ((int (*)(uint32_t, uint32_t))MENU_ITEM_SET_VISIBLE)(CFW_UpNext_MainItem, count != 0);
    osos_menu_item_changed(CFW_UpNext_MainItem);
}

/* Every player event (track change from Next/Previous on any screen, a song ending, a missing
   file skipped, play state) reaches Now Playing's model here. Apple's button hooks above only
   see buttons pressed outside Now Playing, so this is what keeps Up Next current.
   refresh() returns at once when the queue length is unchanged. */
PATCH_JUMP(NOW_PLAYING_EVENT, 0xe92d41f0, 0xe1a04000, cfw_up_next_np_event);
PATCH_ARM int cfw_up_next_np_event(void *model, void *event)
{
    static int inside;
    int result = cfw_np_event_original(model, event);
    if (!inside) {                  /* a refresh may raise player events itself */
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
    int result = ((int (*)(uint32_t, uint32_t))MENU_ITEM_SET_VISIBLE)(item, visible == 1);
    cfw_up_next_refresh();
    return result;
}

/* Apple's "HandlePrevious" (the Previous button), re-implemented to refresh Up Next after it:
   unlike a song ending, skipping a song never re-checks Now Playing's row, so the
   queue (now one song longer) wasn't re-read. Same calls as Apple's: player vtbl 0x1a0, then
   0x10c(1) or 0xfc. */
static void skip(unsigned slot)
{
    uint8_t *player = ((uint8_t *(*)(void))PLAYER_GET)();
    if (player)
        player -= 20;
    if (!((int (*)(void *))osos_method(player, 0x1a0))(player))
        ((void (*)(void *, int))osos_method(player, slot))(player, 1);
    else
        ((void (*)(void *))osos_method(player, 0xfc))(player);
    cfw_up_next_refresh();
}

/* "HandleNext" is the same with 0x108(1); jump_to_row's PLAYER_NEXT comes through here too. */
PATCH_JUMP(PLAYER_NEXT, 0xe92d4010, 0xeb023157, cfw_up_next_next);
PATCH_ARM void cfw_up_next_next(void)
{
    skip(0x108);
}

PATCH_JUMP(PLAYER_PREVIOUS, 0xe92d4010, 0xeb022beb, cfw_up_next_previous);
PATCH_ARM void cfw_up_next_previous(void)
{
    skip(0x10c);
}

/* Apple's "pop top screen" (0x082896b8): the Menu button on any screen ends here (via the
   controller's back handler at 0x08124778). When it uncovers Up Next (back from Now Playing),
   refresh it if the queue moved meanwhile, or leave it if nothing is up next any more. */
PATCH_JUMP(POP_TOP_SCREEN, 0xe92d4070, 0xe1a05000, cfw_up_next_pop_top);
PATCH_ARM void cfw_up_next_pop_top(uint32_t transition)
{
    cfw_pop_top_original(transition);
    /* Now Playing opened from Up Next lives inside Up Next's screen (the navigator's top stays
       Up Next), so while it shows, reloads look on screen but the hidden list ignores them.
       Menu on Now Playing ends here with Up Next on top again: rebuild it now (a posted action
       is never delivered from here), or leave if nothing is up next any more. */
    if (up_next.screen && navigator_top() == up_next.screen) {
        if (playback_dropped() || !queued(osos_player_get()))
            leave_up_next();
        else
            reload_list(RELOAD_KEEP, 2);
    }
}

/* ---- the list --------------------------------------------------------------------------- */

static void forget_rows(void)
{
    up_next.rows = NULL;           /* owned by the browser's song vector */
    up_next.count = 0;
}

PATCH_ARM uint32_t cfw_up_next_build_songs(void *browser)
{
    if (!browser || browser != up_next.browser)
        return cfw_build_songs_original(browser);
    note_screen();                  /* drawing its rows: Up Next is on top */
    if (FIELD(void *, browser, BROWSER_SONGS))
        return 0;
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    unsigned count = queued(player), first = (unsigned)player->current_index + 1, used = 0;
    struct osos_media_entry **rows =
        ((void *(*)(uint32_t))SONGS_ALLOC)((count + 1) * sizeof(*rows));
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
            struct osos_media_entry *entry = osos_playback_entry(&player->playback, first + i);
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

/* Called from the model destructor (cfw_album_artists_destroy_model) before Apple frees the browser. */
void cfw_up_next_model_destroyed(void *model)
{
    if (model && up_next.browser && FIELD(void *, model, 0x40) == up_next.browser) {
        up_next.browser = NULL;
        up_next.model_reference = NULL;
        up_next.model = NULL;
        up_next.controller = up_next.screen = NULL;
        forget_rows();
    }
}

static void *create_model(void **reference)
{
    /* The selection keeps the last model alive after its screen closes: reuse it. */
    if (up_next.model_reference && up_next.browser) {
        ((void (*)(void **, void **))OSOS_MODEL_REF_ASSIGN)(reference, &up_next.model_reference);
        void *model = ((void *(*)(void **))OSOS_MODEL_REF_GET)(reference);
        if (model && FIELD(void *, model, 0x40) == up_next.browser) {
            up_next.model = model;
            return model;
        }
        ((void (*)(void **))OSOS_MODEL_REF_RELEASE)(reference);
        *reference = NULL;
    }
    void **registry = (void **)OSOS_BROWSER_REGISTRY;
    unsigned available = 0;
    for (unsigned i = 0; i < 4; i++)
        available += registry[i] == NULL;
    if (available < 1)
        return NULL;
    void *model = ((void *(*)(uint32_t))OSOS_MODEL_CREATE)(0);
    if (!model)
        return NULL;
    void *browser = NULL;
    if (((int (*)(void *, void **))OSOS_BROWSER_CREATE)(FIELD(void *, model, 0x40), &browser) ||
        !browser) {
        ((void (*)(void *))osos_method(model, 0x04))(model);
        return NULL;
    }
    FIELD(void *, model, 0x40) = browser;
    FIELD(uint8_t, model, 0x45) = 2;
    ((void (*)(void **, void *, int))OSOS_MODEL_REF_INIT)(reference, model, 0);
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
    /* The configuration Apple's "HandleSongsSelected" applies (MusicMenu_ConfigureLibraryBrowser
       with songs arguments). */
    ((void (*)(void *))osos_method(model, 0x38))(model);
    ((void (*)(void *))osos_method(model, 0xec))(model);
    ((void (*)(void *, uint32_t, uint32_t))osos_method(model, 0x5c))(model, 0x11, 0);
    ((void (*)(void *, uint32_t))osos_method(model, 0x100))(model, 0);
    up_next.browser = FIELD(void *, model, 0x40);
    ((void (*)(void *))OSOS_BROWSER_CLEAR_SONGS)(up_next.browser);   /* rebuild from the queue */
    forget_rows();
    void *selection = ((void *(*)(void))SELECTION_GET)();
    ((void (*)(void *, uint32_t, uint32_t))SELECTION_SET_TITLE)(selection, 0x80, CFW_UpNext_Title);
    ((void (*)(void *, int))SELECTION_SET_ROW)(selection, 0);   /* open at the top */
    up_next.controller = up_next.screen = NULL;
    up_next.stale = 0;
    void *screen = ((void *(*)(void *, uint32_t, uint32_t, uint32_t))OSOS_CREATE_SCREEN_CONTROLLER)(
        ((void *(*)(void))OSOS_CONTROLLER_REGISTRY)(), SONGS_SCREEN, SONGS_LAYOUT, 2);
    if (screen) {
        ((void (*)(void *, void *))OSOS_MODEL_REF_ASSIGN)((uint8_t *)screen + 0xb4, &reference);
        FIELD(uint8_t, screen, 0xb2) = 0;
        ((void (*)(void *, void *))osos_method(controller, 0xd0))(controller, screen);
        up_next.controller = screen;
        note_screen();
    }
    ((void (*)(void *))OSOS_MODEL_REF_RELEASE)(&reference);
}

/* Main / Music menu actions (called first from cfw_album_artists_action). */
int cfw_up_next_action(void *controller, const char *action, uint32_t argument)
{
    int open = cfw_string_equal(action, "CFW_UpNext");
    if (!open && !cfw_string_equal(action, "CFW_UpNextHilited"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    main_view = ((void *(*)(void))OSOS_UI_SELECTION)();
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
        leave_up_next();            /* back on Up Next after playback stopped: nothing left */
        return 1;
    }
    up_next.controller = controller;
    note_screen();
    if (up_next.stale) {
        reload_list(0, 0);          /* the menu would act on old rows: refresh, user re-picks */
        return 1;
    }
    uint32_t menu[8];
    ((void (*)(void *, int))CONTEXT_MENU_INIT)(menu, 0);
    struct osos_context_menu *m = (struct osos_context_menu *)menu;
    /* The row the menu was opened for (Apple stores the selection index; with the reused Up
       Next model that index can lag behind, so prefer the row the list passed in). */
    int selected = row;
    if (selected < 0) {
        void *selection = ((void *(*)(void))OSOS_UI_SELECTION)();
        selected = ((int (*)(void *))osos_method(selection, 0x168))(selection);
    }
    int playing = up_next.top && selected == 0;
    if (!playing) {
        osos_context_menu_append(m, CFW_UPNEXT_REMOVE, "CFW_RemoveFromQueue", 1);
        osos_context_menu_append(m, CFW_PLAY_NEXT, "CFW_PlayNext", 1);
        osos_context_menu_append(m, CFW_PLAY_LAST, "CFW_PlayLast", 1);
    }
    osos_context_menu_append(m, CFW_UPNEXT_ALBUM, "HandleViewAlbum", 1);
    osos_context_menu_append(m, CFW_UPNEXT_ARTIST, "HandleViewArtist", 1);
    osos_context_menu_append(m, CFW_SONG_INFO_LABEL, "CFW_SongInfo", 1);
    osos_context_menu_append(m, STR_CANCEL, "HandleCancel", 0);
    FIELD(int, controller, 0xd4) = selected;
    if (selected != -1)
        ((void (*)(void *, void *))CONTEXT_MENU_SHOW)(controller, menu);
    ((void (*)(void *))CONTEXT_MENU_DESTROY)(menu);
    return 1;
}


/* Remove one queued entry (checked to still be queued); the player lock must be held. */
static int delete_entry(struct osos_player *player, struct osos_media_entry *entry)
{
    struct osos_media_list *list = player->playback.list;
    unsigned count = queued(player), first = (unsigned)player->current_index + 1;
    int found = 0;
    for (unsigned i = 0; i < count && !found; i++)
        found = osos_playback_entry(&player->playback, first + i) == entry;
    if (!found)
        return 0;
    osos_player_stop_artwork(player);
    osos_database_begin_update(osos_list_database(list));
    ((int (*)(struct osos_media_entry *))MEDIA_ENTRY_DELETE)(entry);
    osos_database_end_update(osos_list_database(list));
    osos_player_rebuild_artwork(player);
    osos_player_clear_prepared(player);
    osos_audio_set_playback_source(player->playback_source);
    return 1;
}


static int remove_row(struct osos_player *player, int row)
{
    if (row < 0 || (unsigned)row >= up_next.count || !up_next.rows)
        return 0;
    return delete_entry(player, up_next.rows[row]);
}

/* Selecting a queued song plays it now: the songs queued before it are removed (only the songs
   after it stay up next), then Apple's own Next button moves the player onto it inside the
   current queue. Called from the list's "HandleSelect" action (the list's select never reaches
   PlaySongRow; Apple's song controller plays the row from the library itself). */
static void jump_to_row(int row)
{
    if (row < (int)up_next.top || (unsigned)row >= up_next.count || !up_next.rows)
        return;                   /* the playing song: the list just shows Now Playing */
    up_next.jumping = 1;          /* PLAYER_NEXT refreshes too */
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    struct osos_media_entry *target = up_next.rows[row];
    for (int i = (int)up_next.top; i < row; i++)
        delete_entry(player, up_next.rows[i]);
    unsigned first = (unsigned)player->current_index + 1;
    int ready = queued(player) && osos_playback_entry(&player->playback, first) == target;
    osos_player_unlock(player);
    up_next.rows = NULL;          /* entries above were freed; rebuilt on the next read */
    up_next.count = 0;
    if (up_next.browser)
        ((void (*)(void *))OSOS_BROWSER_CLEAR_SONGS)(up_next.browser);
    if (ready)
        ((void (*)(void))PLAYER_NEXT)();
    up_next.shown = ~0u;
    cfw_up_next_refresh();
    up_next.jumping = 0;
}

PATCH_ARM int cfw_up_next_play_row(void *controller, int row, int a2, int a3)
{
    if (!is_up_next(controller))
        return cfw_play_row_original(controller, row, a2, a3);
    return 1;                     /* never play an Up Next row as a new list */
}

static int pending_highlight = -1;


/* Song-list actions on the Up Next screen; called first from cfw_song_action(). */
int cfw_up_next_song_action(void *controller, const char *action, uint32_t argument)
{
    /* Play/Pause on a highlighted row would play that row as a new list: on Up Next it just
       pauses / resumes, like the Play/Pause button anywhere else. */
    /* Actions also reach the Up Next controller as its screen closes, when the focused list is
       already the main menu: never take that for the Up Next list (refreshing then reset the
       main menu to its top row). */
    /* Hold-menu actions arrive while the menu is the focused list: the Up Next list was already
       seen when the menu opened, and the menu refused to open on stale rows. */
    void *focused = ((void *(*)(void))OSOS_UI_SELECTION)();
    int menu_action = cfw_string_equal(action, "CFW_RemoveFromQueue") ||
                      cfw_string_equal(action, "CFW_PlayNext") ||
                      cfw_string_equal(action, "CFW_PlayLast") ||
                      cfw_string_equal(action, "CFW_SongInfo") ||
                      cfw_string_equal(action, "HandleCancel");
    if (is_up_next(controller) && argument != OSOS_ACTION_SUPPORT_QUERY && focused != main_view &&
        !menu_action) {
        if (playback_dropped()) {
            leave_up_next();        /* back on Up Next after playback stopped: nothing left */
            return 1;
        }
        up_next.controller = controller;
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
            void *selection = ((void *(*)(void))OSOS_UI_SELECTION)();
            int row = ((int (*)(void *))osos_method(selection, 0x168))(selection);
            if (row != -1)
                row = ((int (*)(void *, int))osos_method(controller, 0x130))(controller, row);
            jump_to_row(row);     /* the list itself then shows Now Playing */
        }
        return 1;
    }
    if (cfw_string_equal(action, "CFW_UpNextHighlight") && is_up_next(controller)) {
        if (argument != OSOS_ACTION_SUPPORT_QUERY && pending_highlight >= 0 &&
            up_next_on_screen()) {
            void *selection = ((void *(*)(void))SELECTION_GET)();
            unsigned count = queued(osos_player_get());
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
            ((void (*)(void))PLAYER_PLAY_PAUSE)();
        return 1;
    }
    int remove = cfw_string_equal(action, "CFW_RemoveFromQueue");
    int next = cfw_string_equal(action, "CFW_PlayNext");
    int last = cfw_string_equal(action, "CFW_PlayLast");
    if (!remove && !next && !last)
        return 0;
    if (!is_up_next(controller))
        return 0;                     /* Play Next elsewhere: osos-lab's own handler */
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    int row = FIELD(int, controller, 0xd4);
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    int top = (int)up_next.top;
    if (row < top) {
        /* the playing song: nothing to move or remove */
    } else if ((next && row > top && (unsigned)row < up_next.count) ||
        (last && (unsigned)row + 1 < up_next.count)) {
        /* Move: insert a copy right after the current song (Play Next) or at the end of the
           queue (Play Last), then drop the old position. */
        struct osos_media_item item;
        osos_media_item_init(&item);
        osos_song_model_get_item(osos_song_controller_model(controller), row, &item);
        struct osos_media_entry *old = up_next.rows[row];
        if (osos_media_item_valid(&item)) {
            cfw_play_next_add(player, &item, last);
            /* rows[] still points at the old entry; remove it by identity. */
            up_next.rows[row] = old;
            remove_row(player, row);
        }
    } else if (remove) {
        remove_row(player, row);
    }
    osos_player_unlock(player);
    /* Highlight: after a remove, the song above it; after a move, where the song went. Then
       the double blink Apple uses to confirm Add to On-The-Go. */
    unsigned count = queued(player);
    int highlight = remove ? row - 1 : next ? (int)up_next.top : (int)(count + up_next.top) - 1;
    up_next.shown = count;          /* reload once, here, with this highlight */
    reload_list(highlight, 1);
    if (remove && count) {          /* count 0: Up Next was popped, its controller is gone */
        pending_highlight = highlight < 0 ? 0 : highlight;
        post_action(controller, "CFW_UpNextHighlight");
    }
    write_message(count);
    if (settings_model)
        osos_model_notify(settings_model, OSOS_NOTIFY_ALL, CFW_UpNext_Count);
    ((int (*)(uint32_t, uint32_t))MENU_ITEM_SET_VISIBLE)(CFW_UpNext_MainItem, count != 0);
    osos_menu_item_changed(CFW_UpNext_MainItem);
    return 1;
}
