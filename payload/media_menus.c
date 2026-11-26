/* SPDX-License-Identifier: GPL-3.0-only */
/* Contextual menus for movie and TV episode lists, which have none natively. */
#include "osos.h"
#include "rvx/runtime.h"
#include "patch.h"

/* Menus receive an already mapped model row. Extensions add every row; the menu
 * shows only when one of them did. */
static int video_menu(void *controller, int row, int tv)
{
    struct osos_media_item item;
    osos_media_item_init(&item);
    void *model = osos_song_controller_model(controller);
    if (row < 0 || !model)
        return 1;
    osos_song_model_get_item(model, row, &item);
    if (!item.record || !osos_media_item_valid(&item))
        return 1;
    uint32_t storage[6]; /* Native menus live on the caller's stack (24 bytes). */
    struct osos_context_menu *list = (struct osos_context_menu *)storage;
    osos_context_menu_init(list);
    if (cfw_rvx_video_menu(list, controller, row, &item, tv)) {
        osos_context_menu_append(list, OSOS_CANCEL_LABEL, "HandleCancel", 0);
        osos_context_menu_show(controller, list);
    }
    osos_context_menu_destroy(list);
    return 1;
}

PATCH_ARM int cfw_movie_menu(void *controller, int row)
{
    return video_menu(controller, row, 0);
}

PATCH_ARM int cfw_tv_episode_menu(void *controller, int row)
{
    return video_menu(controller, row, 1);
}
