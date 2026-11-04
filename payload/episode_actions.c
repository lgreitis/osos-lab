/* SPDX-License-Identifier: GPL-3.0-only */
#include "osos.h"
#include "cfw.h"
#include "patch.h"
#include "chapters.h"
#include "chapters_ui.h"
#include "native/chapters.h"

/* The native menu receives an already mapped model index, excluding the All row.
 * Capture it before the contextual menu replaces the global selection. */
static struct {
    void *controller;
    int row;
    struct osos_media_item item;
    int available;
} episode_menu;

static uint32_t episode_chapter_count(void *controller, int row,
                                      struct osos_media_item *item)
{
    osos_media_item_init(item);
    void *model = osos_song_controller_model(controller);
    if (row < 0 || !model)
        return 0;
    osos_song_model_get_item(model, row, item);
    return cfw_chapters_count(item);
}

PATCH_ARM int cfw_episode_menu(void *controller, int row)
{
    episode_menu.controller = controller;
    episode_menu.row = row;
    episode_menu.available =
        episode_chapter_count(controller, row, &episode_menu.item) != 0;
    return osos_episode_menu(controller, row);
}

PATCH_ARM void cfw_episode_menu_cancel(struct osos_context_menu *menu, uint32_t label,
                                       const char *action, uint32_t flags)
{
    if (episode_menu.available)
        osos_context_menu_append(menu, CFW_CHAPTERS_LABEL, "CFW_EpisodeChapters", 1);
    osos_context_menu_append(menu, label, action, flags);
}

PATCH_ARM int cfw_episode_action(void *controller, const char *action,
                                 uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_EpisodeChapters"))
        return osos_episode_action(controller, action, argument);
    if (argument != OSOS_ACTION_SUPPORT_QUERY && episode_menu.available &&
        controller == episode_menu.controller) {
        struct osos_media_item item;
        if (episode_chapter_count(controller, episode_menu.row, &item) &&
            cfw_chapters_same_item(&item, &episode_menu.item))
            cfw_chapters_show_episode(controller, &item, episode_menu.row);
    }
    return 1;
}

void cfw_episode_play_chapter(void *controller, int row, struct osos_media_item *item,
                              uint32_t index)
{
    struct osos_media_item selected;
    uint32_t count = episode_chapter_count(controller, row, &selected);
    if (index >= count || !cfw_chapters_same_item(&selected, item))
        return;
    uint32_t start = osos_media_item_chapter_start(&selected, index);
    if (start < osos_media_item_duration(&selected))
        osos_episode_play_chapter(controller, row, start);
}
