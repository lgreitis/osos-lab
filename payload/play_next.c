/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"
#include "cfw.h"
#include "patch.h"
#include "play_next_ui.h"

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

static struct osos_media_entry *insert_song(struct osos_player *player,
                                            struct osos_media_entry *entry,
                                            struct osos_media_entry *current, int last)
{
    struct osos_media_list *list = player->playback.list;
    unsigned count = osos_list_root(list)->tracks;
    int reverse = osos_list_is_reversed(list);
    unsigned position = last ? count : (unsigned)player->current_index + 1;
    unsigned rank = reverse ? count - position : position;
    struct osos_media_entry *after = last ? osos_list_root(list)->group.last : current;
    if (reverse)
        after = last ? NULL : current->previous;
    struct osos_media_entry *cursor[] = {osos_list_root(list), after};

    osos_database_begin_update(osos_list_database(list));
    if (osos_list_is_shuffled(list)) {
        /* The native mode-2 index sorts song ranks at +0x24. Leave one slot open. */
        struct osos_media_order *order = osos_list_shuffle_index(list);
        for (unsigned i = 0; i < count; i++)
            order->entries[i]->song.shuffle_rank = i + (i >= rank);
    }
    struct osos_media_entry *inserted = osos_media_entry_copy(entry, list, cursor, 0);
    if (inserted && osos_list_is_shuffled(list)) {
        inserted->song.shuffle_rank = rank;
        /* Copy notifications can rebuild indexes before the final rank is assigned. */
        osos_media_list_invalidate_order(list, 2);
    }
    osos_database_end_update(osos_list_database(list));
    return inserted;
}

/* Caller holds the player lock. Rebinding the source invalidates cached track order. */
static void invalidate_next(struct osos_player *player)
{
    osos_player_clear_prepared(player);
    osos_audio_set_playback_source(player->playback_source);
}

static struct osos_media_entry *current_entry(struct osos_player *player)
{
    if (!player->current || !osos_playback_list_valid(&player->playback))
        return NULL;

    struct osos_media_list *list = player->playback.list;
    if (osos_list_root(list)->groups || osos_list_root(list)->tracks == UINT16_MAX)
        return NULL;

    struct osos_media_entry *current =
        osos_playback_entry(&player->playback, player->current_index);
    if (!current || current->parent != osos_list_root(list))
        return NULL;
    if (osos_list_is_shuffled(list) &&
        (!osos_list_shuffle_index(list) ||
         osos_list_shuffle_index(list)->count != osos_list_root(list)->tracks))
        return NULL;
    return current;
}

PATCH_ARM void cfw_song_menu_cancel(struct osos_context_menu *menu, uint32_t label,
                                    const char *action, uint32_t flags)
{
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    int available = current_entry(player) != NULL;
    osos_player_unlock(player);
    if (available) {
        osos_context_menu_append(menu, CFW_PLAY_NEXT, "CFW_PlayNext", 1);
        osos_context_menu_append(menu, CFW_PLAY_LAST, "CFW_PlayLast", 1);
    }
    osos_context_menu_append(menu, label, action, flags);
}

static void add_song(void *controller, enum song_source source, int last)
{
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    struct osos_media_entry *current = current_entry(player);
    if (current) {
        struct osos_media_item selected;
        if (selected_item(controller, source, &selected) &&
            osos_media_item_is_music(&selected)) {
            struct osos_media_entry *inserted =
                insert_song(player, osos_media_item_entry(&selected), current, last);
            if (inserted)
                invalidate_next(player);
        }
    }
    osos_player_unlock(player);
}

PATCH_ARM int cfw_song_action(void *controller, const char *action, uint32_t argument)
{
    int last = cfw_string_equal(action, "CFW_PlayLast");
    if (!last && !cfw_string_equal(action, "CFW_PlayNext"))
        return cfw_song_action_original(controller, action, argument);
    if (argument != OSOS_ACTION_SUPPORT_QUERY)
        add_song(controller, SONG_LIST, last);
    return 1;
}

PATCH_ARM int cfw_playlist_action(void *controller, const char *action,
                                  uint32_t argument)
{
    int last = cfw_string_equal(action, "CFW_PlayLast");
    if (!last && !cfw_string_equal(action, "CFW_PlayNext"))
        return cfw_playlist_action_original(controller, action, argument);
    if (argument != OSOS_ACTION_SUPPORT_QUERY)
        add_song(controller, PLAYLIST, last);
    return 1;
}

PATCH_ARM int cfw_genius_action(void *controller, const char *action, uint32_t argument)
{
    int last = cfw_string_equal(action, "CFW_PlayLast");
    if (!last && !cfw_string_equal(action, "CFW_PlayNext"))
        return cfw_genius_action_original(controller, action, argument);
    if (argument != OSOS_ACTION_SUPPORT_QUERY)
        add_song(controller, GENIUS, last);
    return 1;
}
