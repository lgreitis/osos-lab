/* SPDX-License-Identifier: GPL-3.0-only */

/* The Apple objects a UI callback hands out: media items as the context
 * menus hand them out, the player, song models and screen controllers. A
 * MediaItem is a non-owning view of a library record; one built here with
 * MediaItem_InitFromRecord is destroyed by the caller with MediaItem_Destroy.
 * The player's items and lists are read with the player locked. UI task. */

#ifndef RV_MEDIA_H
#define RV_MEDIA_H
#include "osos-2.0.5.h"
#include "rv_api.h"
#include <stddef.h>

static inline struct MediaItem *rv_media_item(const void *item)
{
    return (struct MediaItem *)item;
}

/* The item views a record that is still in the library. */
static inline int rv_item_valid(const void *item)
{
    return item && RV_FIELD(item, MediaItem_record, void *) &&
           MediaItem_IsValid(rv_media_item(item));
}

/* Two items view the same record; the generation tells a record reused after
 * a library reload apart. */
static inline int rv_item_same(const void *a, const void *b)
{
    return RV_FIELD(a, MediaItem_record, void *) ==
               RV_FIELD(b, MediaItem_record, void *) &&
           RV_FIELD(a, MediaItem_generation, uint32_t) ==
               RV_FIELD(b, MediaItem_generation, uint32_t);
}

/* A getter of the item's vtable that returns a word: MediaItem_duration_method,
 * MediaItem_year_method and the rest. */
static inline uint32_t rv_item_value(const void *item, uint32_t slot)
{
    typedef uint32_t (*getter)(const void *);
    return RV_METHOD(item, slot, getter)(item);
}

/* A getter that fills a NativeString, such as MediaItem_title_method: `string`
 * is initialised here and its UTF-8 returned, NULL when empty; the caller
 * destroys `string` once done with the text. */
static inline const char *rv_item_text(const void *item, uint32_t slot,
                                       struct NativeString *string)
{
    typedef void (*getter)(const void *, struct NativeString *);
    NativeString_InitEmpty(string);
    RV_METHOD(item, slot, getter)(item, string);
    const char *text = NativeString_GetUtf8(string);
    return text && *text ? text : NULL;
}

/* The MediaPlayer with its mutex taken, so the playback task cannot change the
 * current track or the playing list until rv_player_unlock. */
static inline void *rv_player_lock(void)
{
    void *player = MediaPlayer_GetInstance();
    Mutex_Lock((uint8_t *)player + MediaPlayer_mutex);
    return player;
}

static inline void rv_player_unlock(void *player)
{
    Mutex_Unlock((uint8_t *)player + MediaPlayer_mutex);
}

/* The playing item into `item`, invalid when nothing plays. */
static inline void rv_player_current(void *player, struct MediaItem *item)
{
    MediaItem_InitFromRecord(item, NULL, 0);
    MediaPlayer_GetCurrentItem(player, item);
}

/* With the player locked: 1 when `item` views the record that plays, with *ms
 * its position. */
static inline int rv_player_position_of(void *player, const void *item, uint32_t *ms)
{
    uint32_t current[MediaItem_bytes / 4];
    rv_player_current(player, rv_media_item(current));
    int playing =
        MediaItem_IsValid(rv_media_item(current)) && rv_item_same(current, item);
    if (playing)
        *ms = MediaPlayer_GetPosition(player);
    MediaItem_Destroy(rv_media_item(current));
    return playing;
}

/* Row `row` of a song model into `item`. */
static inline void rv_model_item(void *model, int32_t row, struct MediaItem *item)
{
    /* Native struct return places the output pointer before this. */
    typedef void (*get_item)(struct MediaItem *, void *, int32_t);
    MediaItem_InitFromRecord(item, NULL, 0);
    RV_METHOD(model, SongModel_get_item_method, get_item)(item, model, row);
}

/* Pushes an Apple screen controller above `controller`, ctx->controller of a
 * UI callback. */
static inline void rv_controller_push(void *controller, void *screen)
{
    typedef void (*push)(void *, void *);
    RV_METHOD(controller, ScreenController_push_method, push)(controller, screen);
}

#endif
