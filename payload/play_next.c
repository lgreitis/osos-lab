/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"
#include "play_next.h"
#include "play_next_ui.h"

static struct osos_media_entry *insert_entry(struct osos_media_list *list,
                                             struct osos_media_entry *entry,
                                             struct osos_media_entry *current,
                                             unsigned current_index, int last)
{
    unsigned count = osos_list_root(list)->tracks;
    int reverse = osos_list_is_reversed(list);
    unsigned position = last ? count : current_index + 1;
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

static struct osos_media_entry *insert_song(struct osos_player *player,
                                            struct osos_media_entry *entry,
                                            struct osos_media_entry *current, int last)
{
    return insert_entry(player->playback.list, entry, current,
                        (unsigned)player->current_index, last);
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
            /* Artwork is indexed by queue position; stop its worker before mutation. */
            osos_player_stop_artwork(player);
            struct osos_media_entry *inserted =
                insert_song(player, osos_media_item_entry(selected), current, last);
            osos_player_rebuild_artwork(player);
            if (inserted)
                invalidate_next(player);
        }
    }
}

void cfw_play_next_add_browser(struct osos_player *player, void *browser, int last)
{
    typedef struct osos_media_list *(*current_list)(void *);
    typedef uint32_t (*build_songs)(void *);
    typedef int (*order_mode)(void *);
    typedef struct osos_media_entry *(*get_entry)(void *, int, uint32_t, uint32_t);
    typedef int (*matches)(void *, struct osos_media_entry *, void *);

    struct osos_media_entry *current = current_entry(player);
    if (!current || !browser)
        return;
    struct osos_media_list *source = ((current_list)OSOS_BROWSER_CURRENT_LIST)(browser);
    if (!source || !osos_list_root(source))
        return;
    /* Same song list and order as the native On-The-Go path for this selection. */
    ((build_songs)OSOS_BROWSER_BUILD_SONGS)(browser);
    int mode = osos_list_is_shuffled(source) ? 2 : ((order_mode)OSOS_BROWSER_ORDER_MODE)(browser);
    uint32_t grouped = *((uint8_t *)browser + OSOS_BROWSER_ORDER_FLAG_OFFSET) != 0;
    unsigned count = osos_list_root(source)->tracks;

    osos_player_stop_artwork(player);
    int inserted = 0;
    for (unsigned i = 0; i < count; i++) {
        /* Play Next inserts each song directly after the current one, so walk backwards. */
        unsigned index = last ? i : count - 1 - i;
        struct osos_media_entry *entry =
            ((get_entry)OSOS_LIST_ORDER_ENTRY)(source, mode, grouped, index);
        if (!entry || !entry->song.record ||
            !((matches)OSOS_BROWSER_FILTER)(browser, entry, source))
            continue;
        struct osos_media_list *queue = player->playback.list;
        if (osos_list_root(queue)->tracks >= UINT16_MAX - 1)
            break;
        /* Each insertion invalidates the shuffle index; rebuild it from the ranks and
           revalidate before insert_song walks it again. */
        if (osos_list_is_shuffled(queue))
            ((get_entry)OSOS_LIST_ORDER_ENTRY)(queue, 2, 0, 0);
        current = current_entry(player);
        if (!current)
            break;
        if (insert_song(player, entry, current, last))
            inserted = 1;
    }
    osos_player_rebuild_artwork(player);
    if (inserted)
        invalidate_next(player);
}

/* Caller holds the player lock. Appends one song to the end of a playlist. A list
   that is playing is the queue itself, so it gets the same care as Play Last.
   player may be NULL while the library loads, when nothing can be playing. */
struct osos_media_entry *cfw_list_append(struct osos_player *player,
                                         struct osos_media_list *list,
                                         struct osos_media_entry *entry)
{
    struct osos_media_entry *root = osos_list_root(list);
    if (!root || root->groups || root->tracks >= UINT16_MAX - 1)
        return NULL;
    int playing = player && player->playback.list == list;
    if (playing && !current_entry(player))
        return NULL;
    int stale = 0;
    if (osos_list_is_shuffled(list)) {
        struct osos_media_order *order = osos_list_shuffle_index(list);
        if (!order || order->count != root->tracks) {
            if (playing)
                return NULL;
            /* Not built for this list yet: append in play order, rebuild later. */
            stale = 1;
        }
    }
    if (playing)
        osos_player_stop_artwork(player);
    struct osos_media_entry *inserted;
    if (stale) {
        struct osos_media_entry *cursor[] = {root, root->group.last};
        if (osos_list_is_reversed(list))
            cursor[1] = NULL;
        osos_database_begin_update(osos_list_database(list));
        inserted = osos_media_entry_copy(entry, list, cursor, 0);
        osos_media_list_invalidate_order(list, 2);
        osos_database_end_update(osos_list_database(list));
    } else {
        inserted = insert_entry(list, entry, NULL, 0, 1);
    }
    if (playing) {
        osos_player_rebuild_artwork(player);
        if (inserted)
            invalidate_next(player);
    }
    return inserted;
}
