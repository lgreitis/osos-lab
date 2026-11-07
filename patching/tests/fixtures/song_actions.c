/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define CFW_PATCH_H
#define PATCH_ARM
#define CFW_OSOS_H
#define OSOS_ACTION_SUPPORT_QUERY 0xdeadbeef
#include "../../../payload/native/bindings.h"

struct osos_media_item {
    void *record;
};

struct osos_player {
    int unused;
};

struct osos_context_menu {
    int unused;
};
static struct osos_player player;
static int record, model, genius, mapped_row = 2, valid = 1;
static uint32_t controller[64];
static unsigned shown, original, queued, locks;
static int create_failed, missing_model, support_query;
static void *owner;
static struct osos_media_item shown_item;

static struct osos_player *osos_player_get(void)
{
    assert(!support_query);
    return &player;
}

static void osos_player_lock(struct osos_player *p)
{
    assert(p == &player);
    locks++;
}

static void osos_player_unlock(struct osos_player *p)
{
    assert(p == &player && locks);
    locks--;
}

static void osos_media_item_init(struct osos_media_item *item)
{
    item->record = NULL;
}

static void osos_player_current_item(struct osos_player *p,
                                     struct osos_media_item *item)
{
    assert(p == &player);
    item->record = &record;
}

static int osos_media_item_valid(struct osos_media_item *item)
{
    return valid && item->record;
}

static void *osos_genius_get(void)
{
    return &genius;
}

static int osos_genius_map_row(void *g, int row)
{
    assert(g == &genius && row == 1);
    return mapped_row;
}

static void osos_genius_get_item(void *g, int row, struct osos_media_item *item)
{
    assert(g == &genius && row == mapped_row);
    item->record = &record;
}

static int osos_song_controller_map_row(void *c, int row)
{
    assert(c == controller && row == 1);
    return mapped_row;
}

static void *osos_song_controller_model(void *c)
{
    assert(c == controller);
    return missing_model ? NULL : &model;
}

static void osos_song_model_get_item(void *m, int row, struct osos_media_item *item)
{
    assert(m == &model && (row == 1 || row == mapped_row));
    item->record = &record;
}

static void osos_context_menu_append(struct osos_context_menu *m, uint32_t label,
                                     const char *action, uint32_t flags)
{
    (void)m;
    (void)label;
    (void)action;
    (void)flags;
}

static unsigned restores, restored_offset;

static void osos_controller_restore_layout(void *c, unsigned offset)
{
    assert(c == controller);
    restores++;
    restored_offset = offset;
}

static int osos_now_playing_action(void *, const char *, uint32_t);
#include "../../../payload/song_actions.c"

int cfw_file_info_show(void *c, struct osos_media_item *item)
{
    owner = c;
    shown_item.record = item ? item->record : NULL;
    shown++;
    return item != NULL && !create_failed;
}

void cfw_play_next_add(struct osos_player *p, struct osos_media_item *item, int last)
{
    (void)p;
    (void)item;
    (void)last;
    queued++;
}

void cfw_play_next_menu(struct osos_context_menu *m)
{
    (void)m;
}

int cfw_song_action_original(void *c, const char *action, uint32_t argument)
{
    (void)c;
    (void)action;
    (void)argument;
    original++;
    return 7;
}

int cfw_playlist_action_original(void *c, const char *a, uint32_t n)
{
    return cfw_song_action_original(c, a, n);
}

int cfw_genius_action_original(void *c, const char *a, uint32_t n)
{
    return cfw_song_action_original(c, a, n);
}

static int osos_now_playing_action(void *c, const char *a, uint32_t n)
{
    return cfw_song_action_original(c, a, n);
}

int main(void)
{
    int (*actions[])(void *, const char *, uint32_t) = {
        cfw_song_action,
        cfw_playlist_action,
        cfw_genius_action,
        cfw_now_playing_action,
    };
    controller[0xd4 / 4] = 1;
    controller[0xe8 / 4] = 1;
    for (unsigned i = 0; i < sizeof(actions) / sizeof(*actions); i++) {
        unsigned layout_offset = i == 3   ? OSOS_NOW_PLAYING_LAYOUT_OFFSET
                                 : i == 1 ? OSOS_PLAYLIST_LAYOUT_OFFSET
                                          : OSOS_SONG_LAYOUT_OFFSET;
        shown = original = queued = restores = 0;
        support_query = 1;
        assert(actions[i](controller, "CFW_FileInfo", OSOS_ACTION_SUPPORT_QUERY));
        assert(!shown && !original && !queued && !restores && !locks);
        support_query = 0;
        assert(actions[i](controller, "CFW_FileInfo", 0));
        assert(owner == controller && shown_item.record == &record);
        assert(shown == 1 && !queued && !original && !restores && !locks);
        create_failed = 1;
        assert(actions[i](controller, "CFW_FileInfo", 0));
        assert(restores == 1 && restored_offset == layout_offset && !locks);
        create_failed = 0;
        restores = 0;
        valid = 0;
        assert(actions[i](controller, "CFW_FileInfo", 0));
        assert(shown_item.record == NULL && restores == 1);
        assert(restored_offset == layout_offset);
        valid = 1;
        assert(actions[i](controller, "HandleCancel", 0) == 7);
        assert(original == 1);
        if (i != 3) {
            assert(actions[i](controller, "CFW_PlayNext", 0));
            assert(actions[i](controller, "CFW_PlayLast", 0));
            assert(queued == 2 && restores == 1 && !locks);
        }
    }
    restores = 0;
    missing_model = 1;
    assert(cfw_song_action(controller, "CFW_FileInfo", 0));
    assert(shown_item.record == NULL);
    assert(restores == 1 && restored_offset == OSOS_SONG_LAYOUT_OFFSET);
    missing_model = 0;
    restores = 0;
    mapped_row = -1;
    assert(cfw_playlist_action(controller, "CFW_FileInfo", 0));
    assert(shown_item.record == NULL);
    assert(restores == 1 && restored_offset == OSOS_PLAYLIST_LAYOUT_OFFSET);
    assert(cfw_genius_action(controller, "CFW_FileInfo", 0));
    assert(shown_item.record == NULL);
    assert(restores == 2 && restored_offset == OSOS_SONG_LAYOUT_OFFSET);
    controller[0xd4 / 4] = UINT32_MAX;
    assert(cfw_song_action(controller, "CFW_FileInfo", 0));
    assert(shown_item.record == NULL);
    assert(restores == 3 && restored_offset == OSOS_SONG_LAYOUT_OFFSET);
    return 0;
}
