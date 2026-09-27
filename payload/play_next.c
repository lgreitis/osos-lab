/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"
#include "cfw.h"
#include "patch.h"
#include "play_next_ui.h"

PATCH_CALL(0x0822E754, 0x08161BCC, cfw_song_menu_cancel);
PATCH_CALL(0x08218188, 0x08161BCC, cfw_song_menu_cancel);
PATCH_CALL(0x0821BE08, 0x08161BCC, cfw_song_menu_cancel);
PATCH_JUMP(0x0822D1B8, 0xE92D41F0, 0xE1A07001, cfw_playlist_action);
PATCH_JUMP(0x082176C0, 0xE92D41F0, 0xE1A07001, cfw_song_action);
PATCH_JUMP(0x0821AC80, 0xE92D41F0, 0xE1A07001, cfw_genius_action);

int cfw_song_action_original(void *controller, const char *action, uint32_t argument);
int cfw_playlist_action_original(void *controller, const char *action,
                                 uint32_t argument);
int cfw_genius_action_original(void *controller, const char *action, uint32_t argument);

enum song_source { SONG_LIST, PLAYLIST, GENIUS };

static int selected_item(void *controller, enum song_source source,
                         struct osos_media_item *selected)
{
    int row = *(int *)((uint8_t *)controller + (source == PLAYLIST ? 0xe8 : 0xd4));
    if (row < 0)
        return 0;

    if (source == GENIUS) {
        void *genius = osos_genius_get();
        row = osos_genius_map_row(genius, row);
        if (row < 0)
            return 0;
        osos_media_item_init(selected);
        osos_genius_get_item(genius, row, selected);
        return osos_media_item_valid(selected);
    }

    if (source == PLAYLIST) {
        row = osos_song_controller_map_row(controller, row);
        if (row < 0)
            return 0;
    }
    void *model = osos_song_controller_model(controller);
    if (!model)
        return 0;
    osos_song_model_get_item(model, row, selected);
    return osos_media_item_valid(selected);
}

static int is_music(struct osos_media_item *item)
{
    struct osos_media_entry *entry = osos_media_item_entry(item);
    return entry && !(entry->flags[1] & 1) &&
           !osos_media_item_has_record_flag_8f_01(item) &&
           !osos_media_item_has_kind_200000(item) &&
           !osos_media_item_has_kind_8(item) && !osos_media_item_has_kind_8062(item);
}

static struct osos_media_entry *copy_after(struct osos_media_list *list,
                                           struct osos_media_entry *entry,
                                           struct osos_media_entry *after)
{
    struct osos_media_entry *cursor[] = {after->parent, after};
    osos_database_begin_update(list->database);
    struct osos_media_entry *inserted = osos_media_entry_copy(entry, list, cursor, 0);
    osos_database_end_update(list->database);
    return inserted;
}

/* Caller holds the player lock. Rebinding the source invalidates cached track order. */
static void invalidate_next(struct osos_player *player)
{
    osos_player_clear_prepared(player);
    osos_audio_set_playback_source(player->playback_source);
}

static struct osos_media_entry *insertion_point(struct osos_player *player)
{
    if (!player->current || !osos_playback_list_valid(&player->playback))
        return NULL;

    struct osos_media_list *list = player->playback.list;
    /* Forward playback; 1 = Repeat All, 2 = Repeat One. */
    if ((list->flags & 1) || (list->order_flags & 4) || list->repeat == 2 ||
        list->root->tracks == UINT16_MAX)
        return NULL;

    return osos_playback_entry(&player->playback, player->current_index);
}

PATCH_ARM void cfw_song_menu_cancel(struct osos_context_menu *menu, uint32_t label,
                                    const char *action, uint32_t flags)
{
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    int available = insertion_point(player) != NULL;
    osos_player_unlock(player);
    if (available)
        osos_context_menu_append(menu, CFW_PLAY_NEXT, "CFW_PlayNext", 1);
    osos_context_menu_append(menu, label, action, flags);
}

static void play_next(void *controller, enum song_source source)
{
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    struct osos_media_entry *current = insertion_point(player);
    if (current) {
        struct osos_media_item selected;
        if (selected_item(controller, source, &selected) && is_music(&selected)) {
            struct osos_media_list *list = player->playback.list;
            struct osos_media_entry *inserted =
                copy_after(list, osos_media_item_entry(&selected), current);
            if (inserted)
                invalidate_next(player);
        }
    }
    osos_player_unlock(player);
}

PATCH_ARM int cfw_song_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_PlayNext"))
        return cfw_song_action_original(controller, action, argument);
    if (argument != OSOS_ACTION_SUPPORT_QUERY)
        play_next(controller, SONG_LIST);
    return 1;
}

PATCH_ARM int cfw_playlist_action(void *controller, const char *action,
                                  uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_PlayNext"))
        return cfw_playlist_action_original(controller, action, argument);
    if (argument != OSOS_ACTION_SUPPORT_QUERY)
        play_next(controller, PLAYLIST);
    return 1;
}

PATCH_ARM int cfw_genius_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_PlayNext"))
        return cfw_genius_action_original(controller, action, argument);
    if (argument != OSOS_ACTION_SUPPORT_QUERY)
        play_next(controller, GENIUS);
    return 1;
}
