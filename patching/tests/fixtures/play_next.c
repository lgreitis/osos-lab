/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert

static struct osos_media_entry root, current, copy;
static struct osos_player player;
static int music, fail_copy, copies, prepared, artwork;

static struct osos_media_entry *list_root(struct osos_media_list *list)
{
    assert(list == player.playback.list);
    return &root;
}

static int list_flag(struct osos_media_list *list)
{
    (void)list;
    return 0;
}

static int playback_valid(struct osos_playback_list *list)
{
    return list->list != NULL;
}

static struct osos_media_entry *playback_entry(struct osos_playback_list *list,
                                               int index)
{
    (void)list;
    assert(index == 0);
    return &current;
}

static void *database(struct osos_media_list *list)
{
    return list;
}

static void update(void *database)
{
    assert(database == player.playback.list);
}

static struct osos_media_entry *entry_copy(struct osos_media_entry *entry,
                                           struct osos_media_list *list,
                                           struct osos_media_entry **cursor, int flags)
{
    (void)flags;
    assert(entry && list == player.playback.list && cursor[0] == &root);
    copies++;
    if (fail_copy)
        return NULL;
    root.tracks++;
    return &copy;
}

static int is_music(struct osos_media_item *item)
{
    (void)item;
    return music;
}

static void stop_artwork(struct osos_player *p)
{
    assert(p == &player && artwork == 0);
    artwork++;
}

static void rebuild_artwork(struct osos_player *p)
{
    assert(p == &player && artwork == 1);
    artwork--;
}

static void clear_prepared(struct osos_player *p)
{
    assert(p == &player);
    prepared++;
}

static void set_source(void *source)
{
    assert(source == player.playback_source);
}

#define osos_list_root list_root
#define osos_list_is_shuffled list_flag
#define osos_list_is_reversed list_flag
#define osos_playback_list_valid playback_valid
#define osos_playback_entry playback_entry
#define osos_list_database database
#define osos_database_begin_update update
#define osos_database_end_update update
#define osos_media_entry_copy entry_copy
#define osos_media_item_is_music is_music
#define osos_player_stop_artwork stop_artwork
#define osos_player_rebuild_artwork rebuild_artwork
#define osos_player_clear_prepared clear_prepared
#define osos_audio_set_playback_source set_source
#include "../../../payload/play_next.c"

int main(void)
{
    player.current = &current;
    player.playback.list = (void *)&root;
    current.parent = &root;
    root.tracks = 2;
    struct osos_media_item item = {.entry = &current};
    assert(!cfw_play_next_add(&player, NULL, 0));
    assert(!cfw_play_next_add(&player, &item, 0));
    assert(copies == 0 && root.tracks == 2);
    music = 1;
    fail_copy = 1;
    assert(!cfw_play_next_add(&player, &item, 0));
    assert(!cfw_play_next_add(&player, &item, 1));
    assert(copies == 2 && root.tracks == 2 && prepared == 0 && artwork == 0);
    fail_copy = 0;
    assert(cfw_play_next_add(&player, &item, 0));
    assert(cfw_play_next_add(&player, &item, 1));
    assert(root.tracks == 4 && prepared == 2 && artwork == 0);
    player.current = NULL;
    assert(!cfw_play_next_add(&player, &item, 0));
    assert(copies == 4 && prepared == 2);
    return 0;
}
