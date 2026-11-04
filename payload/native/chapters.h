/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_NATIVE_CHAPTERS_H
#define CFW_NATIVE_CHAPTERS_H

static inline uint32_t osos_media_item_chapter_start(struct osos_media_item *item,
                                                     uint32_t index)
{
    typedef uint32_t (*fn)(struct osos_media_item *, uint32_t);
    return ((fn)osos_method(item, OSOS_MEDIA_CHAPTER_START_SLOT))(item, index);
}

/* Native title lookup takes a timestamp, not a chapter index. */
static inline void osos_media_item_chapter_title(struct osos_media_item *item,
                                                 uint32_t position,
                                                 struct osos_native_string *title)
{
    typedef int (*fn)(struct osos_media_item *, uint32_t, struct osos_native_string *);
    ((fn)osos_method(item, OSOS_MEDIA_CHAPTER_TITLE_SLOT))(item, position, title);
}

/* Like native chapter skipping, seek takes the recursive player mutex. */
static inline void osos_player_seek(struct osos_player *player, uint32_t position)
{
    typedef int (*fn)(struct osos_player *, uint32_t);
    ((fn)osos_method(player, OSOS_PLAYER_SEEK_SLOT))(player, position);
}

static inline int osos_episode_menu(void *controller, int row)
{
    typedef int (*fn)(void *, int);
    return ((fn)OSOS_EPISODE_CONTEXT_MENU)(controller, row);
}

static inline int osos_episode_action(void *controller, const char *action,
                                      uint32_t argument)
{
    typedef int (*fn)(void *, const char *, uint32_t);
    return ((fn)OSOS_EPISODE_ACTION)(controller, action, argument);
}

/* Match the native audiobook chapter handler at 0x08231bb0. Its start-time
 * property is consumed on Now Playing activation, after the queued transition. */
static inline void osos_episode_play_chapter(void *controller, int row,
                                             uint32_t position)
{
    void *interface =
        (uint8_t *)osos_player_get() + OSOS_PLAYER_CHAPTER_INTERFACE_OFFSET;
    typedef void (*prepare_fn)(void *);
    ((prepare_fn)osos_method(interface, OSOS_PLAYER_CHAPTER_PREPARE_SLOT))(interface);
    struct osos_string key;
    osos_string_init(&key, "NowPlayingStartTime");
    typedef void (*set_fn)(void *, struct osos_string *, uint32_t *);
    ((set_fn)OSOS_PROPERTY_SET_INTEGER)((void *)OSOS_PLAYBACK_PROPERTIES, &key,
                                        &position);
    osos_string_destroy(&key);
    typedef void (*play_fn)(void *, int, int, uint32_t);
    ((play_fn)OSOS_LIBRARY_PLAY)(controller, row, 0, UINT32_MAX);
}
#endif
