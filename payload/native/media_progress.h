/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/* Apple 2.0.5 contracts; see docs/osos.md. */
static inline int osos_media_progress_icon_property(void *model, uint32_t type,
                                                    uint32_t property, int row,
                                                    uintptr_t *out)
{
    typedef int (*fn)(void *, uint32_t, uint32_t, int, uintptr_t *);
    return ((fn)OSOS_MEDIA_LIST_PROPERTY)(model, type, property, row, out);
}

static inline void osos_media_progress_icon_item(void *model, int row,
                                                 struct osos_media_item *item)
{
    void *source = osos_model_ref_get((uint8_t *)model + OSOS_MEDIA_LIST_SOURCE_OFFSET);
    if (source)
        osos_song_model_get_item(source, row, item);
}

static inline uint32_t osos_media_progress_player_position(struct osos_player *player)
{
    typedef uint32_t (*fn)(struct osos_player *);
    return ((fn)OSOS_PLAYER_POSITION)(player);
}

static inline void osos_media_progress_icon_set(void *drawable, const void *bitmap)
{
    typedef void (*fn)(void *, const void *, uint32_t, uint32_t);
    ((fn)OSOS_BITMAP_DRAWABLE_SET)(drawable, bitmap, 0, 0);
}
