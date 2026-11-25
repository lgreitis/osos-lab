/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert

struct test_queue {
    struct osos_media_entry root;
    struct osos_media_entry *physical[64];

    struct {
        uint32_t reserved, capacity, bytes, count;
        struct osos_media_entry *entries[64];
    } order;

    int shuffled, reversed, order_valid;
};
static struct test_queue queue, source;
static struct osos_media_entry entries[128];
static struct osos_media_record records[128];
static struct osos_player player;
static unsigned allocated, copies, deletes, stops, rebuilds, prepared, rebound, locks;
static unsigned lookups; /* playback-list entry reads, to watch a drain's cost */
static int artwork_stopped, update_depth, music = 1, fail_copy, fail_delete;
static unsigned filtered_id = UINT32_MAX;
static uint8_t browser_selection[OSOS_BROWSER_ORDER_FLAG_OFFSET + 1];

static inline struct test_queue *test_list(struct osos_media_list *list)
{
    return (struct test_queue *)list;
}

static inline void relink(struct test_queue *list)
{
    unsigned count = list->root.tracks;
    list->root.group.first = count ? list->physical[0] : NULL;
    list->root.group.last = count ? list->physical[count - 1] : NULL;
    for (unsigned i = 0; i < count; i++) {
        struct osos_media_entry *entry = list->physical[i];
        entry->parent = &list->root;
        entry->list = (void *)list;
        entry->previous = i ? list->physical[i - 1] : NULL;
        entry->next = i + 1 < count ? list->physical[i + 1] : NULL;
    }
    list->order_valid = 0;
}

static inline void rebuild_order(struct test_queue *list)
{
    unsigned count = list->root.tracks;
    assert(count <= 64);
    memcpy(list->order.entries, list->physical, count * sizeof(void *));
    for (unsigned i = 1; i < count; i++) {
        struct osos_media_entry *entry = list->order.entries[i];
        unsigned j = i;
        while (j && list->order.entries[j - 1]->song.shuffle_rank >
                        entry->song.shuffle_rank) {
            list->order.entries[j] = list->order.entries[j - 1];
            j--;
        }
        list->order.entries[j] = entry;
    }
    list->order.count = count;
    list->order.capacity = 64;
    list->order_valid = 1;
}

static inline struct osos_media_entry *queue_root(struct osos_media_list *list)
{
    return &test_list(list)->root;
}

static inline int queue_shuffled(struct osos_media_list *list)
{
    return test_list(list)->shuffled;
}

static inline int queue_reversed(struct osos_media_list *list)
{
    return test_list(list)->reversed;
}

static inline struct osos_media_order *queue_shuffle_index(struct osos_media_list *list)
{
    struct test_queue *q = test_list(list);
    return q->order_valid ? (void *)&q->order : NULL;
}

static inline int playback_valid(struct osos_playback_list *list)
{
    return list->list != NULL;
}

static inline struct osos_media_entry *queue_entry(struct osos_playback_list *view,
                                                   int index)
{
    struct test_queue *list = test_list(view->list);
    lookups++;
    if (!list || index < 0 || (unsigned)index >= list->root.tracks)
        return NULL;
    if (list->shuffled && !list->order_valid)
        rebuild_order(list);
    unsigned position =
        list->reversed ? list->root.tracks - 1 - (unsigned)index : (unsigned)index;
    assert(position < 64);
    return list->shuffled ? list->order.entries[position] : list->physical[position];
}

static inline void *queue_database(struct osos_media_list *list)
{
    return list;
}

static inline void begin_update(void *database)
{
    assert(database == &queue && !update_depth);
    update_depth++;
}

static inline void end_update(void *database)
{
    assert(database == &queue && update_depth == 1);
    update_depth--;
}

static inline void invalidate_order(struct osos_media_list *list, unsigned mode)
{
    assert(mode == 2);
    test_list(list)->order_valid = 0;
}

static inline struct osos_media_entry *copy_entry(struct osos_media_entry *entry,
                                                  struct osos_media_list *native,
                                                  struct osos_media_entry **cursor,
                                                  int flags)
{
    struct test_queue *list = test_list(native);
    assert(list == &queue && cursor[0] == &queue.root && flags == 0);
    assert(artwork_stopped && update_depth);
    copies++;
    if (fail_copy)
        return NULL;
    unsigned index = 0;
    if (cursor[1]) {
        while (index < list->root.tracks && list->physical[index] != cursor[1])
            index++;
        assert(index < list->root.tracks);
        index++;
    }
    assert(list->root.tracks < 64 && allocated < 128);
    memmove(list->physical + index + 1, list->physical + index,
            (list->root.tracks - index) * sizeof(void *));
    struct osos_media_entry *copy = &entries[allocated++];
    *copy = *entry;
    list->physical[index] = copy;
    list->root.tracks++;
    relink(list);
    return copy;
}

static inline int delete_queue_entry(struct osos_media_entry *entry)
{
    assert(artwork_stopped && update_depth);
    if (fail_delete && deletes == (unsigned)(fail_delete - 1))
        return -50;
    unsigned i = 0;
    while (i < queue.root.tracks && queue.physical[i] != entry)
        i++;
    assert(i < queue.root.tracks);
    memmove(queue.physical + i, queue.physical + i + 1,
            (--queue.root.tracks - i) * sizeof(void *));
    entry->parent = NULL;
    relink(&queue);
    deletes++;
    return 0;
}

static inline int item_music(struct osos_media_item *item)
{
    return music && item && item->record;
}

static inline int item_valid(struct osos_media_item *item)
{
    return item && item->record;
}

static inline struct osos_media_entry *item_entry(struct osos_media_item *item)
{
    return item->entry ? item->entry : item->record->entries;
}

static inline void item_init_record(struct osos_media_item *item,
                                    struct osos_media_record *record)
{
    memset(item, 0, sizeof(*item));
    item->record = record;
}

static inline struct osos_player *get_player(void)
{
    return &player;
}

static inline void lock_player(struct osos_player *p)
{
    assert(p == &player);
    locks++;
}

static inline void unlock_player(struct osos_player *p)
{
    assert(p == &player && locks);
    locks--;
}

static inline void stop_artwork(struct osos_player *p)
{
    assert(p == &player && !artwork_stopped);
    artwork_stopped = 1;
    stops++;
}

static inline int rebuild_artwork(struct osos_player *p)
{
    assert(p == &player && artwork_stopped);
    artwork_stopped = 0;
    rebuilds++;
    return 1;
}

static inline void clear_prepared(struct osos_player *p)
{
    assert(p == &player);
    prepared++;
}

static inline void set_source(void *s)
{
    assert(s == player.playback_source);
    rebound++;
}

static inline struct osos_media_list *browser_list(void *b)
{
    assert(b == browser_selection);
    return (void *)&source;
}

static inline uint32_t build_browser_songs(void *b)
{
    assert(b == browser_selection);
    return source.root.tracks;
}

static inline int browser_mode(void *b)
{
    assert(b == browser_selection);
    return 6;
}

static inline struct osos_media_entry *
order_entry(struct osos_media_list *list, int mode, uint32_t grouped, unsigned index)
{
    (void)mode;
    (void)grouped;
    struct osos_playback_list view = {.list = list};
    return queue_entry(&view, (int)index);
}

static inline int browser_filter(void *b, struct osos_media_entry *entry,
                                 struct osos_media_list *list)
{
    assert(b == browser_selection && list == (void *)&source);
    return entry->id != filtered_id;
}

static inline void reset_queue(int shuffled, int reversed)
{
    memset(&queue, 0, sizeof(queue));
    memset(&source, 0, sizeof(source));
    memset(&player, 0, sizeof(player));
    memset(entries, 0, sizeof(entries));
    memset(records, 0, sizeof(records));
    allocated = copies = deletes = stops = rebuilds = prepared = rebound = locks = 0;
    lookups = 0;
    artwork_stopped = update_depth = fail_copy = fail_delete = 0;
    music = 1;
    filtered_id = UINT32_MAX;
    for (unsigned i = 0; i < 8; i++) {
        struct osos_media_entry *entry = &entries[allocated++];
        entry->id = i;
        entry->song.record = &records[i];
        records[i].entries = entry;
        entry->song.shuffle_rank = i;
        struct test_queue *list = i < 5 ? &queue : &source;
        list->physical[list->root.tracks++] = entry;
    }
    relink(&queue);
    relink(&source);
    queue.shuffled = shuffled;
    queue.reversed = reversed;
    const unsigned ranks[] = {3, 1, 4, 0, 2};
    if (shuffled)
        for (unsigned i = 0; i < 5; i++)
            queue.physical[i]->song.shuffle_rank = ranks[i];
    rebuild_order(&queue);
    player.playback.list = (void *)&queue;
    player.current_index = 1;
    player.current = queue_entry(&player.playback, player.current_index);
}

/* Extend the queue behind the current song, as a long Play Next buildup would. */
static inline void grow_queue(unsigned extra)
{
    while (extra && queue.root.tracks < 64 && allocated < 128) {
        struct osos_media_entry *entry = &entries[allocated];
        memset(entry, 0, sizeof(*entry));
        entry->id = 100 + allocated;
        entry->song.record = &records[allocated];
        records[allocated].entries = entry;
        entry->song.shuffle_rank = allocated;
        queue.physical[queue.root.tracks++] = entry;
        allocated++;
        extra--;
    }
    relink(&queue);
    rebuild_order(&queue);
}

#define osos_list_root queue_root
#define osos_list_is_shuffled queue_shuffled
#define osos_list_is_reversed queue_reversed
#define osos_list_shuffle_index queue_shuffle_index
#define osos_playback_list_valid playback_valid
#define osos_playback_entry queue_entry
#define osos_list_database queue_database
#define osos_database_begin_update begin_update
#define osos_database_end_update end_update
#define osos_media_list_invalidate_order invalidate_order
#define osos_media_entry_copy copy_entry
#define osos_media_entry_delete delete_queue_entry
#define osos_media_item_is_music item_music
#define osos_media_item_valid item_valid
#define osos_media_item_entry item_entry
#define osos_media_item_init_record item_init_record
#define osos_player_get get_player
#define osos_player_lock lock_player
#define osos_player_unlock unlock_player
#define osos_player_stop_artwork stop_artwork
#define osos_player_rebuild_artwork rebuild_artwork
#define osos_player_clear_prepared clear_prepared
#define osos_audio_set_playback_source set_source
#define osos_browser_current_list browser_list
#define osos_browser_build_songs build_browser_songs
#define osos_browser_order_mode browser_mode
#define osos_list_order_entry order_entry
#define osos_browser_filter browser_filter
