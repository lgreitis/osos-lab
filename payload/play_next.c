/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"
#include "play_next.h"
#include "play_next_ui.h"

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

void cfw_play_next_menu(struct osos_context_menu *menu)
{
    struct osos_player *player = osos_player_get();
    osos_player_lock(player);
    int available = current_entry(player) != NULL;
    osos_player_unlock(player);
    if (available) {
        osos_context_menu_append(menu, CFW_PLAY_NEXT, "CFW_PlayNext", 1);
        osos_context_menu_append(menu, CFW_PLAY_LAST, "CFW_PlayLast", 1);
    }
}

void cfw_play_next_add(struct osos_player *player, struct osos_media_item *selected, int last)
{
    struct osos_media_entry *current = current_entry(player);
    if (current) {
        if (selected && osos_media_item_is_music(selected)) {
            struct osos_media_entry *inserted =
                insert_song(player, osos_media_item_entry(selected), current, last);
            if (inserted)
                invalidate_next(player);
        }
    }
}
