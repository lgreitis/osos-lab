/* SPDX-License-Identifier: GPL-3.0-only */
#include "media_queue.h"
#include "up_next_ui.h"

#define CFW_PATCH_H
#define PATCH_ARM
/* Keep the native pointer fields separate and aligned on the 64-bit host. */
#undef OSOS_BROWSER_SONGS_OFFSET
#undef OSOS_BROWSER_SONGS_JUMP_OFFSET
#undef OSOS_BROWSER_SONGS_COUNT_OFFSET
#define OSOS_BROWSER_SONGS_OFFSET 0x80
#define OSOS_BROWSER_SONGS_JUMP_OFFSET 0x88
#define OSOS_BROWSER_SONGS_COUNT_OFFSET 0x90

static union {
    max_align_t alignment;
    uint8_t data[256];
} browser, model, selection, controller, other_model;

static int view, navigator_screen;
static void *focused_view = &view;
static uint32_t screen_id = OSOS_SONGS_SCREEN;
static unsigned reloads, restores, pops, blinks, posts, menu_changes, notifications;
static unsigned play_calls, pause_calls, skip_calls, original_calls, menu_rows;
static const char *menu_actions[8];
static uint32_t menu_labels[8];
static int highlighted, played_index, available, allocation_fails;
static char message[40];
static void *registry[4];

uint32_t cfw_up_next_build_songs(void *);

static void *selection_get(void)
{
    return selection.data;
}

static void *ui_selection(void)
{
    return focused_view;
}

static void *navigator_top(void)
{
    return &navigator_screen;
}

static uint32_t get_screen_id(void *screen)
{
    assert(screen == &navigator_screen);
    return screen_id;
}

static void *controller_model(void *c)
{
    if (c == controller.data)
        return model.data;
    assert(c == other_model.data);
    return other_model.data;
}

static void pop_main(void)
{
    pops++;
    screen_id = 0;
}

static void clear_songs(void *b)
{
    assert(b == browser.data);
    void **songs = (void **)(browser.data + OSOS_BROWSER_SONGS_OFFSET);
    free(*songs);
    *songs = NULL;
    *(uint32_t *)(browser.data + OSOS_BROWSER_SONGS_COUNT_OFFSET) = 0;
}

static void reload_selection(void *s)
{
    assert(s == selection.data);
    reloads++;
    highlighted = 0;
    cfw_up_next_build_songs(browser.data);
}

static void selection_row(void *s, int row)
{
    assert(s == selection.data);
    *(int *)(selection.data + 0x2c) = row;
}

static void view_row(void *v, int row)
{
    assert(v == &view);
    highlighted = row;
}

static int get_view_row(void *v)
{
    assert(v == &view);
    return highlighted;
}

static void blink_row(int row)
{
    assert(row >= 0);
    blinks++;
}

static void restore_layout(void *c, unsigned offset)
{
    assert(c == controller.data && offset == OSOS_SONG_LAYOUT_OFFSET);
    restores++;
}

static struct osos_media_entry **allocate_songs(uint32_t bytes)
{
    return allocation_fails ? NULL : calloc(1, bytes);
}

static void *resource_bank(void)
{
    return &view;
}

static void set_resource(void *bank, uint32_t type, uint32_t id, const void *text,
                         uint32_t bytes)
{
    assert(bank == &view && type == OSOS_PROPERTY_STRING && id == CFW_UpNext_Message);
    assert(bytes <= sizeof(message));
    memcpy(message, text, bytes);
}

static uintptr_t ui_string(uint32_t id)
{
    return id;
}

static void model_notify(void *m, uint32_t type, uint32_t property)
{
    assert(m == model.data && type == OSOS_NOTIFY_ALL && property == CFW_UpNext_Count);
    notifications++;
}

static void menu_changed(uint32_t item)
{
    assert(item == CFW_UpNext_MainItem);
    menu_changes++;
}

void cfw_ui_visibility_set_available(uint32_t item, int state)
{
    assert(item == CFW_UpNext_MainItem);
    available = state;
}

static int play_index(struct osos_player *p, int index)
{
    assert(p == &player && !locks);
    play_calls++;
    played_index = index;
    player.current_index = index;
    player.current = queue_entry(&player.playback, index);
    return 1;
}

static void play_pause(void)
{
    pause_calls++;
}

static void skip_player(int previous)
{
    (void)previous;
    skip_calls++;
}

static int map_row(void *c, int row)
{
    assert(c == controller.data);
    return row;
}

static void init_string(struct osos_string *s, const char *text)
{
    s->data = (char *)text;
}

static void destroy_string(struct osos_string *s)
{
    (void)s;
}

static void mock_post_action(void *c, struct osos_string *action)
{
    assert(c == controller.data && !strcmp(action->data, "CFW_UpNextHighlight"));
    posts++;
}

static void menu_init(struct osos_context_menu *m)
{
    assert(m);
    menu_rows = 0;
}

static void menu_append(struct osos_context_menu *m, uint32_t label, const char *action,
                        uint32_t flags)
{
    assert(m && action && menu_rows < 8);
    (void)flags;
    menu_labels[menu_rows] = label;
    menu_actions[menu_rows] = action;
    menu_rows++;
}

/* The row the hold menu shows for this action, or -1 when it offers none. */
static int menu_row_of(const char *name)
{
    for (unsigned i = 0; i < menu_rows; i++)
        if (!strcmp(menu_actions[i], name))
            return (int)i;
    return -1;
}

static void menu_show(void *c, struct osos_context_menu *m)
{
    assert(c == controller.data && m);
}

static void menu_destroy(struct osos_context_menu *m)
{
    assert(m);
}

static void read_song(struct osos_media_item *out, void *m, int row)
{
    assert(m == model.data);
    struct osos_media_entry **rows =
        *(void **)(browser.data + OSOS_BROWSER_SONGS_OFFSET);
    unsigned count = *(uint32_t *)(browser.data + OSOS_BROWSER_SONGS_COUNT_OFFSET);
    memset(out, 0, sizeof(*out));
    if (row >= 0 && (unsigned)row < count) {
        out->entry = rows[row];
        out->record = rows[row]->song.record;
    }
}

void *cfw_media_browser_create(void **reference)
{
    *reference = model.data;
    return model.data;
}

static void ref_assign(void *out, void *in)
{
    *(void **)out = *(void **)in;
}

static void *ref_get(void *ref)
{
    return *(void **)ref;
}

static void ref_release(void *ref)
{
    *(void **)ref = NULL;
}

static void prepare_songs(void *m)
{
    assert(m == model.data);
}

static void selection_title(void *s, uint32_t kind, uint32_t title)
{
    assert(s == selection.data && kind == 0x80 && title == CFW_UpNext_Title);
}

static void *controller_registry(void)
{
    return registry;
}

static void *create_screen(void *r, uint32_t screen, uint32_t layout,
                           uint32_t transition)
{
    assert(r == registry && screen == OSOS_SONGS_SCREEN &&
           layout == OSOS_SONGS_LAYOUT && transition == 2);
    return controller.data;
}

static void push_controller(void *c, void *screen)
{
    (void)c;
    assert(screen == controller.data);
}

static int set_menu_visible(uint32_t item, uint32_t state)
{
    (void)item;
    return state;
}

#define osos_selection_get selection_get
#define osos_ui_selection ui_selection
#define osos_navigator_top navigator_top
#define osos_screen_id get_screen_id
#define osos_song_controller_model controller_model
#define osos_pop_to_main_screen pop_main
#define osos_browser_clear_songs clear_songs
#define osos_selection_reload reload_selection
#define osos_selection_set_row selection_row
#define osos_list_view_set_row view_row
#define osos_list_view_row get_view_row
#define osos_row_blink blink_row
#define osos_controller_restore_layout restore_layout
#define osos_songs_alloc allocate_songs
#define osos_resource_bank resource_bank
#define osos_resource_set set_resource
#define osos_ui_string ui_string
#define osos_model_notify model_notify
#define osos_menu_item_changed menu_changed
#define osos_player_play_index play_index
#define osos_player_play_pause play_pause
#define osos_player_skip skip_player
#define osos_song_controller_map_row map_row
#define osos_string_init init_string
#define osos_string_destroy destroy_string
#define osos_controller_post_action mock_post_action
#define osos_context_menu_init menu_init
#define osos_context_menu_append menu_append
#define osos_context_menu_show menu_show
#define osos_context_menu_destroy menu_destroy
#define osos_library_read_song_item read_song
#define osos_model_ref_assign ref_assign
#define osos_model_ref_get ref_get
#define osos_model_ref_release ref_release
#define osos_library_model_prepare_songs prepare_songs
#define osos_selection_set_title selection_title
#define osos_controller_registry controller_registry
#define osos_create_screen_controller create_screen
#define osos_controller_push push_controller
#define osos_menu_item_set_visible set_menu_visible
#include "../../../payload/play_next.c"
#include "../../../payload/up_next.c"

uint32_t cfw_build_songs_original(void *b)
{
    (void)b;
    original_calls++;
    return 7;
}

int cfw_song_menu_original(void *c, int row)
{
    (void)c;
    (void)row;
    original_calls++;
    return 7;
}

int cfw_play_row_original(void *c, int row, int a2, int a3)
{
    (void)c;
    (void)row;
    (void)a2;
    (void)a3;
    original_calls++;
    return 7;
}

void cfw_pop_top_original(uint32_t transition)
{
    (void)transition;
    original_calls++;
}

int cfw_np_event_original(void *m, void *e)
{
    (void)m;
    (void)e;
    original_calls++;
    return 7;
}

static void setup(int shuffled, int reversed)
{
    clear_songs(browser.data);
    reset_queue(shuffled, reversed);
    memset(&up_next, 0, sizeof(up_next));
    memset(&model, 0, sizeof(model));
    memset(&selection, 0, sizeof(selection));
    memset(&controller, 0, sizeof(controller));
    *(void **)(model.data + 0x40) = browser.data;
    *(void **)(selection.data + 0x18) = model.data;
    up_next.browser = browser.data;
    up_next.model = model.data;
    up_next.model_reference = model.data;
    up_next.shown = queued(&player);
    up_next.list_view = &view;
    screen_id = OSOS_SONGS_SCREEN;
    focused_view = &view;
    main_view = NULL;
    queue_generation = menu_generation = 0;
    queue_changed = 0;
    settings_model = model.data;
    pending_highlight = -1;
    reloads = restores = pops = blinks = posts = menu_changes = notifications = 0;
    play_calls = pause_calls = skip_calls = original_calls = menu_rows = 0;
    highlighted = played_index = allocation_fails = 0;
    cfw_up_next_build_songs(browser.data);
    assert(up_next.count == 4 && up_next.top == 1);
    for (unsigned i = 0; i < 4; i++)
        assert(up_next.rows[i] == queue_entry(&player.playback, i + 1));
}

static void action(const char *name, int row)
{
    *(int *)(controller.data + 0xd4) = row;
    assert(cfw_up_next_song_action(controller.data, name, 0));
    assert(!locks && !artwork_stopped && !update_depth);
}

static void check_edits(int shuffled, int reversed)
{
    setup(shuffled, reversed);
    unsigned moved = up_next.rows[2]->id;
    action("CFW_PlayNext", 2);
    assert(queue.root.tracks == 5 && queue_entry(&player.playback, 2)->id == moved);
    assert(copies == 1 && deletes == 1 && stops == 1 && rebuilds == 1 &&
           prepared == 1 && rebound == 1);
    assert(highlighted == 1 && !up_next.stale);
    assert(cfw_up_next_song_menu(controller.data, 1));
    int clear = menu_row_of("CFW_ClearUpNext");
    assert(menu_rows == 8 && clear == 3); /* after the per-song queue edits */
    assert(menu_labels[clear] == CFW_UPNEXT_CLEAR);
    setup(shuffled, reversed);
    moved = up_next.rows[1]->id;
    action("CFW_PlayLast", 1);
    assert(queue.root.tracks == 5 && queue_entry(&player.playback, 4)->id == moved);
    assert(stops == 1 && rebuilds == 1 && rebound == 1 && highlighted == 3);
    setup(shuffled, reversed);
    action("CFW_RemoveFromQueue", 2);
    assert(queue.root.tracks == 4 && deletes == 1 && stops == 1 && rebuilds == 1 &&
           posts == 1);
    action("CFW_UpNextHighlight", 1);
    assert(highlighted == 1 && pending_highlight == -1);
    setup(shuffled, reversed);
    action("CFW_RemoveFromQueue", 0);
    action("CFW_PlayNext", 1);
    action("CFW_PlayLast", 3);
    assert(!copies && !deletes && !stops);
    setup(shuffled, reversed);
    moved = up_next.rows[3]->id;
    highlighted = 3;
    action("HandleSelect", 3);
    assert(deletes == 2 && stops == 1 && rebuilds == 1 && prepared == 1 &&
           rebound == 1);
    assert(play_calls == 1 && played_index == 2 &&
           queue_entry(&player.playback, 2)->id == moved);
}

/* Clearing empties the queue, closes the screen and reports an empty main-menu row. */
static void check_clear(int shuffled, int reversed)
{
    struct osos_media_entry *played;
    setup(shuffled, reversed);
    played = queue_entry(&player.playback, 0); /* the song that already played */
    action("CFW_ClearUpNext", 3);
    assert(deletes == 3 && queue.root.tracks == 2 && !queued(&player));
    assert(!queue_entry(&player.playback, 2));
    assert(player.current == queue_entry(&player.playback, 1));
    assert(queue_entry(&player.playback, 0) == played);
    assert(stops == 1 && rebuilds == 1 && prepared == 1 && rebound == 1);
    assert(pops == 1 && !up_next.count && !up_next.stale);
    assert(!strcmp(message, "0 Songs") && !available && menu_changes == 1 &&
           notifications == 1);

    setup(shuffled, reversed);
    action("CFW_ClearUpNext", 0); /* opened on the playing song, which has no row actions */
    assert(deletes == 3 && queue.root.tracks == 2 && pops == 1);
    assert(player.current == queue_entry(&player.playback, 1));
}

static void check_failures_and_stale(void)
{
    setup(0, 0);
    fail_copy = 1;
    action("CFW_PlayNext", 2);
    assert(!deletes && queue.root.tracks == 5 && !prepared && stops == rebuilds);
    setup(0, 0);
    fail_delete = 2;
    highlighted = 3;
    action("HandleSelect", 3);
    assert(deletes == 1 && !play_calls && stops == 1 && rebuilds == 1 && rebound == 1);
    setup(0, 0);
    queue_generation++;
    action("CFW_RemoveFromQueue", 2);
    assert(restores == 1 && !deletes && !stops);
    setup(0, 0);
    up_next.stale = 1;
    action("HandleSelect", 2);
    assert(!play_calls && !deletes && !up_next.stale);
    setup(0, 0);
    clear_songs(browser.data);
    forget_rows();
    allocation_fails = 1;
    assert(!cfw_up_next_build_songs(browser.data) && !up_next.rows && !up_next.count);
    setup(0, 0);
    player.current = NULL;
    action("HandleSelect", 2);
    assert(pops == 1 && !play_calls);
}

/* Clear only acts on the Up Next screen, on rows the menu really offered. */
static void check_clear_guards(void)
{
    assert(!cfw_up_next_song_action(other_model.data, "CFW_ClearUpNext", 0));
    setup(0, 0);
    assert(cfw_up_next_song_action(controller.data, "CFW_ClearUpNext",
                                   OSOS_ACTION_SUPPORT_QUERY));
    assert(!deletes && !stops && queue.root.tracks == 5);
    setup(0, 0);
    queue_generation++; /* the queue moved on while the menu was open */
    action("CFW_ClearUpNext", 2);
    assert(restores == 1 && !deletes && !stops && queue.root.tracks == 5);
    setup(0, 0);
    up_next.stale = 1; /* covered by Now Playing: refresh instead of acting */
    action("CFW_ClearUpNext", 2);
    assert(!deletes && !stops && reloads == 1 && !up_next.stale);
    setup(0, 0);
    fail_delete = 2; /* the second song refuses to go: keep the rest of the queue */
    action("CFW_ClearUpNext", 2);
    assert(deletes == 1 && queue.root.tracks == 4 && stops == 1 && rebound == 1);
    assert(!pops && highlighted == 0 && available &&
           !strcmp(message, "2 Songs"));
}

static void check_refresh_and_lifecycle(void)
{
    setup(0, 0);
    assert(cfw_up_next_build_songs(&other_model) == 7);
    assert(cfw_up_next_song_menu(&other_model, 0) == 7);
    assert(cfw_up_next_play_row(&other_model, 0, 0, 0) == 7);
    assert(cfw_up_next_play_row(controller.data, 1, 0, 0) == 1);
    assert(cfw_up_next_song_action(controller.data, "CFW_PlayNext",
                                   OSOS_ACTION_SUPPORT_QUERY));
    assert(!stops);
    cfw_up_next_song_menu(controller.data, 0);
    assert(menu_rows == 5 && menu_row_of("CFW_ClearUpNext") == 0);
    cfw_up_next_song_menu(controller.data, 1);
    assert(menu_rows == 8 && menu_row_of("CFW_RemoveFromQueue") == 0);
    action("HandlePlayPause", 1);
    assert(pause_calls == 1);
    up_next.shown = ~0u;
    cfw_up_next_refresh();
    assert(!strcmp(message, "3 Songs") && available && menu_changes == 1 &&
           notifications == 1);
    unsigned generation = queue_generation;
    cfw_up_next_refresh();
    assert(queue_generation == generation && menu_changes == 1);
    uint32_t event[] = {0, 0x80009};
    cfw_up_next_np_event(model.data, event);
    assert(queue_generation == generation + 1 && menu_changes == 2);
    clear_songs(browser.data);
    forget_rows();
    struct osos_media_item item;
    cfw_up_next_song_item(&item, model.data, 1);
    assert(item.record == queue_entry(&player.playback, 2)->song.record);
    *(void **)(other_model.data + 0x40) = browser.data;
    assert(cfw_up_next_owns_model(model.data));
    assert(!cfw_up_next_owns_model(other_model.data));
    cfw_up_next_model_destroyed(other_model.data);
    assert(up_next.model == model.data && up_next.rows);
    cfw_up_next_model_destroyed(model.data);
    assert(!*(void **)(browser.data + OSOS_BROWSER_SONGS_OFFSET));
    assert(!*(uint32_t *)(browser.data + OSOS_BROWSER_SONGS_COUNT_OFFSET));
    assert(!up_next.model && !up_next.browser && !up_next.rows &&
           !up_next.model_reference);
    clear_songs(browser.data);
}

int main(void)
{
    for (int shuffled = 0; shuffled < 2; shuffled++)
        for (int reversed = 0; reversed < 2; reversed++) {
            check_edits(shuffled, reversed);
            check_clear(shuffled, reversed);
        }
    check_failures_and_stale();
    check_clear_guards();
    check_refresh_and_lifecycle();
    return 0;
}
