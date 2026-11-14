/* SPDX-License-Identifier: GPL-3.0-only */
/* File Info in the contextual menus of podcast, iTunes U, audiobook and video lists. */
#include "osos.h"
#include "cfw.h"
#include "file_info.h"
#include "file_info_ui.h"
#include "media_menus.h"
#include "patch.h"

/* Menus receive an already mapped model row. Capture the item before the menu
 * replaces the global selection, and check it again when the action runs. */
static struct {
    void *controller;
    int row;
    unsigned layout_offset;
    struct osos_media_item item;
    int available;
} menu;

static int list_item(void *controller, int row, struct osos_media_item *item)
{
    osos_media_item_init(item);
    void *model = osos_song_controller_model(controller);
    if (row < 0 || !model)
        return 0;
    osos_song_model_get_item(model, row, item);
    return item->record && osos_media_item_valid(item);
}

/* +0x10 (reserved[1]) is the database generation. */
static int same_item(struct osos_media_item *a, struct osos_media_item *b)
{
    return a->record == b->record && a->reserved[1] == b->reserved[1];
}

static void capture(void *controller, int row, unsigned layout_offset)
{
    menu.controller = controller;
    menu.row = row;
    menu.layout_offset = layout_offset;
    menu.available = list_item(controller, row, &menu.item);
}

int cfw_media_menu_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_FileInfo"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    struct osos_media_item item;
    if (menu.available && controller == menu.controller &&
        list_item(controller, menu.row, &item) && same_item(&item, &menu.item) &&
        cfw_file_info_show(controller, &item))
        return 1;
    osos_controller_restore_layout(controller, menu.layout_offset);
    return 1;
}

/* Native menus end with Cancel; File Info goes right before it. */
PATCH_ARM void cfw_media_menu_cancel(struct osos_context_menu *list, uint32_t label,
                                     const char *action, uint32_t flags)
{
    if (menu.available)
        osos_context_menu_append(list, CFW_FILE_INFO_LABEL, "CFW_FileInfo", 1);
    osos_context_menu_append(list, label, action, flags);
}

static int native_menu(uintptr_t original, void *controller, int row)
{
    typedef int (*fn)(void *, int);
    return ((fn)original)(controller, row);
}

PATCH_ARM int cfw_episode_menu(void *controller, int row)
{
    capture(controller, row, OSOS_EPISODE_LAYOUT_OFFSET);
    return native_menu(OSOS_EPISODE_CONTEXT_MENU, controller, row);
}

PATCH_ARM int cfw_lecture_menu(void *controller, int row)
{
    capture(controller, row, OSOS_LECTURE_LAYOUT_OFFSET);
    return native_menu(OSOS_LECTURE_CONTEXT_MENU, controller, row);
}

PATCH_ARM int cfw_audiobook_menu(void *controller, int row)
{
    capture(controller, row, OSOS_AUDIOBOOK_LAYOUT_OFFSET);
    return native_menu(OSOS_AUDIOBOOK_CONTEXT_MENU, controller, row);
}

/* Movies and TV episodes have no native contextual menu. */
static int video_menu(void *controller, int row, unsigned layout_offset)
{
    capture(controller, row, layout_offset);
    if (!menu.available)
        return 1;
    uint32_t storage[6]; /* Native menus live on the caller's stack (24 bytes). */
    struct osos_context_menu *list = (struct osos_context_menu *)storage;
    osos_context_menu_init(list);
    osos_context_menu_append(list, CFW_FILE_INFO_LABEL, "CFW_FileInfo", 1);
    osos_context_menu_append(list, OSOS_CANCEL_LABEL, "HandleCancel", 0);
    osos_context_menu_show(controller, list);
    osos_context_menu_destroy(list);
    return 1;
}

PATCH_ARM int cfw_movie_menu(void *controller, int row)
{
    return video_menu(controller, row, OSOS_MOVIE_LAYOUT_OFFSET);
}

PATCH_ARM int cfw_tv_episode_menu(void *controller, int row)
{
    return video_menu(controller, row, OSOS_TV_EPISODE_LAYOUT_OFFSET);
}

static int native_action(uintptr_t original, void *controller, const char *action,
                         uint32_t argument)
{
    if (cfw_media_menu_action(controller, action, argument))
        return 1;
    typedef int (*fn)(void *, const char *, uint32_t);
    return ((fn)original)(controller, action, argument);
}

PATCH_ARM int cfw_episode_action(void *controller, const char *action,
                                 uint32_t argument)
{
    return native_action(OSOS_EPISODE_ACTION, controller, action, argument);
}

PATCH_ARM int cfw_lecture_action(void *controller, const char *action,
                                 uint32_t argument)
{
    return native_action(OSOS_LECTURE_ACTION, controller, action, argument);
}
