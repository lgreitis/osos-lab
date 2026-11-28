/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM
#include "../../../payload/native/bindings.h"

struct osos_media_record;

struct osos_media_item {
    uintptr_t *vtable;
    void *entry;
    struct osos_media_record *record;
    uint32_t reserved[3];
};

struct osos_context_menu;

static int controller, model, missing_model, valid = 1;
static uint8_t records[4];
static unsigned menu_inits, menu_shows, menu_destroys, rvx_calls, rvx_rows;
static char appended[4][32];
static uint32_t appended_labels[4];
static unsigned appended_count;
static int rvx_row, rvx_tv;
static const struct osos_media_item *rvx_item;

static void osos_media_item_init(struct osos_media_item *item)
{
    memset(item, 0, sizeof(*item));
}

static void *osos_song_controller_model(void *c)
{
    assert(c == &controller);
    return missing_model ? NULL : &model;
}

static void osos_song_model_get_item(void *m, int row, struct osos_media_item *item)
{
    assert(m == &model && row >= 0 && row < 4);
    item->record = (struct osos_media_record *)&records[row];
}

static int osos_media_item_valid(struct osos_media_item *item)
{
    return valid && item->record;
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

unsigned cfw_rvx_video_menu(void *menu, void *c, int row, const void *item, int tv)
{
    assert(menu && c == &controller);
    rvx_calls++;
    rvx_row = row;
    rvx_tv = tv;
    rvx_item = item;
    for (unsigned i = 0; i < rvx_rows; i++)
        osos_context_menu_append(menu, 1, "RVX", 1);
    return rvx_rows;
}

#include "../../../payload/media_menus.c"

static void reset(void)
{
    appended_count = 0;
    menu_inits = menu_shows = menu_destroys = rvx_calls = 0;
}

int main(void)
{
    int (*menus[])(void *, int) = {cfw_movie_menu, cfw_tv_episode_menu};
    for (int tv = 0; tv < 2; tv++) {
        /* Extension rows get Cancel after them. */
        reset();
        rvx_rows = 1;
        assert(menus[tv](&controller, 2) == 1);
        assert(rvx_calls == 1 && rvx_row == 2 && rvx_tv == tv);
        assert(menu_inits == 1 && menu_shows == 1 && menu_destroys == 1);
        assert(appended_count == 2 && !strcmp(appended[0], "RVX"));
        assert(appended_labels[1] == OSOS_CANCEL_LABEL);
        assert(!strcmp(appended[1], "HandleCancel"));
        /* No extension rows: no menu. */
        reset();
        rvx_rows = 0;
        assert(menus[tv](&controller, 2) == 1);
        assert(rvx_calls == 1 && menu_inits == 1 && !menu_shows && menu_destroys == 1);
        assert(!appended_count);
    }
    /* Invalid rows ask no extension. */
    reset();
    rvx_rows = 1;
    assert(cfw_movie_menu(&controller, -1) == 1 && !rvx_calls && !menu_inits);
    missing_model = 1;
    assert(cfw_movie_menu(&controller, 1) == 1 && !rvx_calls);
    missing_model = 0;
    valid = 0;
    assert(cfw_tv_episode_menu(&controller, 1) == 1 && !rvx_calls);
    return 0;
}
