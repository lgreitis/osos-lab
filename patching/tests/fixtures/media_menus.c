/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM
#define OSOS_ACTION_SUPPORT_QUERY 0xdeadbeef
#include "../../../payload/native/bindings.h"
#include "file_info_ui.h"

static int native_menu_call(void *, int);
static int native_action_call(void *, const char *, uint32_t);
#undef OSOS_EPISODE_CONTEXT_MENU
#define OSOS_EPISODE_CONTEXT_MENU ((uintptr_t)native_menu_call)
#undef OSOS_LECTURE_CONTEXT_MENU
#define OSOS_LECTURE_CONTEXT_MENU ((uintptr_t)native_menu_call)
#undef OSOS_AUDIOBOOK_CONTEXT_MENU
#define OSOS_AUDIOBOOK_CONTEXT_MENU ((uintptr_t)native_menu_call)
#undef OSOS_EPISODE_ACTION
#define OSOS_EPISODE_ACTION ((uintptr_t)native_action_call)
#undef OSOS_LECTURE_ACTION
#define OSOS_LECTURE_ACTION ((uintptr_t)native_action_call)

struct osos_media_record;

struct osos_media_item {
    uintptr_t *vtable;
    void *entry;
    struct osos_media_record *record;
    uint32_t reserved[3];
};

struct osos_context_menu;

static int controller, other_controller, model, missing_model, show_fails, valid = 1;
static uint8_t records[4];
static uint32_t generation = 7;
static unsigned native_menus, native_actions, shown, restored, restored_offset;
static unsigned menu_inits, menu_shows, menu_destroys;
static char appended[4][32];
static uint32_t appended_labels[4];
static unsigned appended_count;
static struct osos_media_item shown_item;

static int native_menu_call(void *c, int row)
{
    (void)c;
    (void)row;
    native_menus++;
    return 1;
}

static int native_action_call(void *c, const char *action, uint32_t argument)
{
    (void)c;
    (void)action;
    (void)argument;
    native_actions++;
    return 7;
}

static void osos_media_item_init(struct osos_media_item *item)
{
    memset(item, 0, sizeof(*item));
}

static void *osos_song_controller_model(void *c)
{
    assert(c == &controller || c == &other_controller);
    return missing_model ? NULL : &model;
}

static void osos_song_model_get_item(void *m, int row, struct osos_media_item *item)
{
    assert(m == &model && row >= 0 && row < 4);
    item->record = (struct osos_media_record *)&records[row];
    item->reserved[1] = generation;
}

static int osos_media_item_valid(struct osos_media_item *item)
{
    return valid && item->record;
}

static void osos_controller_restore_layout(void *c, unsigned offset)
{
    assert(c == &controller || c == &other_controller);
    restored++;
    restored_offset = offset;
}

static void osos_context_menu_append(struct osos_context_menu *menu, uint32_t label,
                                     const char *action, uint32_t flags)
{
    assert(menu && appended_count < 4);
    (void)flags;
    appended_labels[appended_count] = label;
    strcpy(appended[appended_count++], action);
}

static void osos_context_menu_init(struct osos_context_menu *menu)
{
    assert(menu);
    menu_inits++;
}

static void osos_context_menu_show(void *c, struct osos_context_menu *menu)
{
    assert(c == &controller && menu);
    menu_shows++;
}

static void osos_context_menu_destroy(struct osos_context_menu *menu)
{
    assert(menu);
    menu_destroys++;
}

int cfw_file_info_show(void *c, struct osos_media_item *item)
{
    assert(c == &controller);
    shown++;
    shown_item = *item;
    return !show_fails;
}

#include "../../../payload/media_menus.c"

static void reset(void)
{
    appended_count = 0;
    shown = restored = native_menus = native_actions = 0;
    menu_inits = menu_shows = menu_destroys = 0;
}

static void check_native_menus(void)
{
    struct osos_context_menu *list = (struct osos_context_menu *)&model;
    int (*menus[])(void *, int) = {cfw_episode_menu, cfw_lecture_menu,
                                   cfw_audiobook_menu};
    const unsigned offsets[] = {OSOS_EPISODE_LAYOUT_OFFSET, OSOS_LECTURE_LAYOUT_OFFSET,
                                OSOS_AUDIOBOOK_LAYOUT_OFFSET};
    for (unsigned i = 0; i < 3; i++) {
        reset();
        assert(menus[i](&controller, 2) == 1 && native_menus == 1);
        cfw_media_menu_cancel(list, OSOS_CANCEL_LABEL, "HandleCancel", 0);
        assert(appended_count == 2 && !strcmp(appended[0], "CFW_FileInfo"));
        assert(appended_labels[0] == CFW_FILE_INFO_LABEL);
        assert(!strcmp(appended[1], "HandleCancel"));
        /* Support queries never open anything. */
        assert(cfw_media_menu_action(&controller, "CFW_FileInfo",
                                     OSOS_ACTION_SUPPORT_QUERY));
        assert(!shown && !restored);
        assert(cfw_media_menu_action(&controller, "CFW_FileInfo", 0));
        assert(shown == 1 && shown_item.record == (void *)&records[2]);
        show_fails = 1;
        assert(cfw_media_menu_action(&controller, "CFW_FileInfo", 0));
        assert(restored == 1 && restored_offset == offsets[i]);
        show_fails = 0;
    }
    /* Invalid rows offer no File Info. */
    reset();
    cfw_episode_menu(&controller, -1);
    cfw_media_menu_cancel(list, OSOS_CANCEL_LABEL, "HandleCancel", 0);
    assert(appended_count == 1 && !strcmp(appended[0], "HandleCancel"));
    assert(cfw_media_menu_action(&controller, "CFW_FileInfo", 0));
    assert(!shown && restored == 1);
    missing_model = 1;
    cfw_episode_menu(&controller, 1);
    missing_model = 0;
    assert(cfw_media_menu_action(&controller, "CFW_FileInfo", 0) && !shown);
}

static void check_revalidation(void)
{
    reset();
    cfw_episode_menu(&controller, 1);
    assert(cfw_media_menu_action(&other_controller, "CFW_FileInfo", 0));
    assert(!shown && restored == 1);
    generation++; /* A library reload replaces the selected record. */
    assert(cfw_media_menu_action(&controller, "CFW_FileInfo", 0));
    assert(!shown && restored == 2);
    cfw_episode_menu(&controller, 1);
    valid = 0;
    assert(cfw_media_menu_action(&controller, "CFW_FileInfo", 0));
    assert(!shown && restored == 3);
    valid = 1;
}

static void check_video_menus(void)
{
    int (*menus[])(void *, int) = {cfw_movie_menu, cfw_tv_episode_menu};
    const unsigned offsets[] = {OSOS_MOVIE_LAYOUT_OFFSET,
                                OSOS_TV_EPISODE_LAYOUT_OFFSET};
    for (unsigned i = 0; i < 2; i++) {
        reset();
        assert(menus[i](&controller, 0) == 1);
        assert(menu_inits == 1 && menu_shows == 1 && menu_destroys == 1);
        assert(appended_count == 2 && !strcmp(appended[0], "CFW_FileInfo"));
        assert(appended_labels[1] == OSOS_CANCEL_LABEL);
        assert(!strcmp(appended[1], "HandleCancel") && !native_menus);
        show_fails = 1;
        assert(cfw_media_menu_action(&controller, "CFW_FileInfo", 0));
        assert(restored_offset == offsets[i]);
        show_fails = 0;
    }
    reset();
    assert(cfw_movie_menu(&controller, -1) == 1 && !menu_inits && !appended_count);
}

static void check_forwarding(void)
{
    reset();
    assert(!cfw_media_menu_action(&controller, "HandleAddToOTG", 0));
    assert(!cfw_media_menu_action(&controller, NULL, 0));
    assert(cfw_episode_action(&controller, "HandleAddToOTG", 0) == 7);
    assert(cfw_lecture_action(&controller, "HandleSelect", 0) == 7);
    assert(native_actions == 2 && !shown);
    cfw_episode_menu(&controller, 3);
    assert(cfw_episode_action(&controller, "CFW_FileInfo", 0) == 1);
    assert(native_actions == 2 && shown == 1);
}

int main(void)
{
    check_native_menus();
    check_revalidation();
    check_video_menus();
    check_forwarding();
    return 0;
}
