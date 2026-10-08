/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_ALBUM_ARTISTS_INDEX_H
#define CFW_ALBUM_ARTISTS_INDEX_H
#include <stdint.h>

struct album_artist_track {
    struct osos_media_entry *entry;
    uint32_t token, order;
};

typedef int (*album_artist_compare)(void *, uint32_t, uint32_t);

static int album_artist_track_compare(const struct album_artist_track *a,
                                      const struct album_artist_track *b,
                                      album_artist_compare compare, void *context)
{
    int result = compare(context, a->token, b->token);
    return result ? result : (a->order > b->order) - (a->order < b->order);
}

static void album_artist_sift(struct album_artist_track *tracks, uint32_t root,
                              uint32_t count, album_artist_compare compare, void *context)
{
    while (root < count / 2) {
        uint32_t child = root * 2 + 1;
        if (child + 1 < count && album_artist_track_compare(
                &tracks[child], &tracks[child + 1], compare, context) < 0)
            child++;
        if (album_artist_track_compare(&tracks[root], &tracks[child], compare, context) >= 0)
            break;
        struct album_artist_track swap = tracks[root];
        tracks[root] = tracks[child];
        tracks[child] = swap;
        root = child;
    }
}

/* Preserve native album order within collation-equivalent artist names. */
static void album_artist_sort(struct album_artist_track *tracks, uint32_t count,
                              album_artist_compare compare, void *context)
{
    for (uint32_t i = count / 2; i; i--)
        album_artist_sift(tracks, i - 1, count, compare, context);
    for (uint32_t i = count; i > 1; i--) {
        struct album_artist_track swap = tracks[0];
        tracks[0] = tracks[i - 1];
        tracks[i - 1] = swap;
        album_artist_sift(tracks, 0, i - 1, compare, context);
    }
}
#endif
