/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Cover Flow song actions.
 *
 * Stock Cover Flow has no hold menu. On the back of an album (its track list),
 * holding Center now opens one with Play Next, Play Last and Add to Playlist for
 * the highlighted song (coverflow_actions.ui binds the hold and the choices to
 * the backside layout only).
 */
#include "osos.h"
#include "cfw.h"
#include "patch.h"
#include "patches/sites.h"
#include "play_next.h"
#include "playlist_add.h"
#include "coverflow_actions_ui.h"

/* Enough room for the native menu object (24 bytes on the stock builders). */
struct menu_storage {
    uint32_t words[16];
};

static int shown_album(void *controller)
{
    uint8_t *flow = *(uint8_t **)((uint8_t *)controller + OSOS_COVERFLOW_FLOW);
    return flow ? *(int *)(flow + OSOS_COVERFLOW_FLOW_ALBUM) : -1;
}

static int highlighted_song(void *controller, struct osos_media_item *item)
{
    typedef void *(*get)(void);
    typedef int (*highlighted)(void *);
    typedef void (*item_at)(struct osos_media_item *, void *, int);
    if (shown_album(controller) < 0)
        return 0;
    void *view = ((get)OSOS_LIST_VIEW_MODEL)();
    int row = view ? ((highlighted)osos_method(view, OSOS_LIST_VIEW_HIGHLIGHTED))(view) : -1;
    uint8_t *model = ((get)OSOS_COVERFLOW_MODEL_GET)();
    void *browser = model ? *(void **)(model + OSOS_COVERFLOW_MODEL_BROWSER) : NULL;
    if (row < 0 || !browser)
        return 0;
    ((item_at)osos_method(browser, OSOS_BROWSER_ITEM_AT))(item, browser, row);
    if (osos_media_item_valid(item))
        return 1;
    ((void (*)(struct osos_media_item *))OSOS_MEDIA_ITEM_DESTROY)(item);
    return 0;
}

static void show_menu(void *controller)
{
    typedef void (*init)(struct menu_storage *, int);
    typedef void (*show)(void *, struct menu_storage *);
    typedef void (*destroy)(struct menu_storage *);
    struct menu_storage menu;
    struct osos_context_menu *items = (struct osos_context_menu *)&menu;
    ((init)OSOS_CONTEXT_MENU_INIT)(&menu, 0);
    cfw_play_next_menu(items);
    osos_context_menu_append(items, CFW_COVERFLOW_ADD_TO_PLAYLIST, "CFW_AddToPlaylist", 1);
    osos_context_menu_append(items, CFW_COVERFLOW_CANCEL, "HandleCancel", 0);
    ((show)OSOS_CONTROLLER_SHOW_CONTEXT_MENU)(controller, &menu);
    ((destroy)OSOS_CONTEXT_MENU_DESTROY)(&menu);
}

static int song_action(void *controller, const char *action, uint32_t argument)
{
    int menu = cfw_string_equal(action, "CFW_CoverFlowMenu");
    int add = cfw_string_equal(action, "CFW_AddToPlaylist");
    int last = cfw_string_equal(action, "CFW_PlayLast");
    if (!menu && !add && !last && !cfw_string_equal(action, "CFW_PlayNext"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    struct osos_media_item item;
    int found = highlighted_song(controller, &item);
    if (menu) {
        if (found)
            show_menu(controller);
    } else if (add) {
        /* The screen event then opens the playlist chooser. */
        cfw_playlist_add_select(found ? &item : NULL);
        cfw_playlist_add_from_coverflow(shown_album(controller));
    } else if (found) {
        struct osos_player *player = osos_player_get();
        osos_player_lock(player);
        cfw_play_next_add(player, &item, last);
        osos_player_unlock(player);
    }
    if (found)
        ((void (*)(struct osos_media_item *))OSOS_MEDIA_ITEM_DESTROY)(&item);
    return 1;
}

PATCH_ARM int cfw_coverflow_action(void *controller, const char *action, uint32_t argument)
{
    typedef int (*dispatch)(void *, const char *, uint32_t);
    if (song_action(controller, action, argument))
        return 1;
    return ((dispatch)OSOS_COVERFLOW_ACTION)(controller, action, argument);
}
