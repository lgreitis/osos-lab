/* SPDX-License-Identifier: GPL-3.0-only */
/* Play Next and Play Last for whole albums and artists. */
#include "osos.h"
#include "cfw.h"
#include "patch.h"
#include "play_next.h"
#include "up_next.h"

int cfw_media_list_action_original(void *, const char *, uint32_t);

/* Play Last flag for the native add currently in progress, or -1 when idle. */
static int queue_last = -1;

PATCH_ARM void cfw_selection_menu_cancel(struct osos_context_menu *menu, uint32_t label,
                                         const char *action, uint32_t flags)
{
    cfw_play_next_menu(menu);
    osos_context_menu_append(menu, label, action, flags);
}

PATCH_ARM int cfw_media_list_action(void *controller, const char *action, uint32_t argument)
{
    int last = cfw_string_equal(action, "CFW_PlayLast");
    if (!last && !cfw_string_equal(action, "CFW_PlayNext"))
        return cfw_media_list_action_original(controller, action, argument);
    if (argument != OSOS_ACTION_SUPPORT_QUERY) {
        /* Reuse Add to On-The-Go: it filters the library to the selected album or
           artist, then reaches cfw_queue_selection instead of the OTG playlist. */
        queue_last = last;
        ((int (*)(void *))OSOS_MEDIA_LIST_ADD_TO_OTG)(controller);
        queue_last = -1;
        cfw_up_next_refresh();
    }
    return 1;
}

PATCH_ARM int cfw_queue_selection(void *otg, int row, void *browser, int save)
{
    typedef int (*add)(void *, int, void *, int);
    if (queue_last < 0)
        return ((add)OSOS_OTG_ADD_BROWSER)(otg, row, browser, save);
    /* Album and artist rows always add their whole filtered selection. */
    if (row != -1)
        return 0;
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    cfw_play_next_add_browser(player, browser, queue_last);
    osos_player_unlock(player);
    return 1;
}
