/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_ALBUM_ARTISTS_INDEX_H
#define CFW_ALBUM_ARTISTS_INDEX_H
#include <stdint.h>

struct album_artist_track {
    struct osos_media_entry *entry;

    union {
        uint32_t token;
        uint32_t group_index;
    };

    uint32_t order;
};

struct artist_group {
    uint32_t token, display_token;
    uint32_t *artwork;
    uint32_t artwork_count, albums;
};

typedef int (*album_artist_compare)(void *, const struct album_artist_track *,
                                    const struct album_artist_track *);

static int album_artist_track_compare(const struct album_artist_track *a,
                                      const struct album_artist_track *b,
                                      album_artist_compare compare, void *context)
{
    int result = compare(context, a, b);
    return result ? result : (a->order > b->order) - (a->order < b->order);
}

static void album_artist_sift(struct album_artist_track *tracks, uint32_t root,
                              uint32_t count, album_artist_compare compare,
                              void *context)
{
    while (root < count / 2) {
        uint32_t child = root * 2 + 1;
        if (child + 1 < count &&
            album_artist_track_compare(&tracks[child], &tracks[child + 1], compare,
                                       context) < 0)
            child++;
        if (album_artist_track_compare(&tracks[root], &tracks[child], compare,
                                       context) >= 0)
            break;
        struct album_artist_track swap = tracks[root];
        tracks[root] = tracks[child];
        tracks[child] = swap;
        root = child;
    }
}

/* Keep the incoming track order as the final tie-breaker. */
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

static inline void album_artist_reorder(struct artist_group *rows,
                                        struct album_artist_track *order,
                                        uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        struct artist_group saved = rows[i];
        uint32_t current = i;
        while (order[current].group_index != i) {
            uint32_t next = order[current].group_index;
            rows[current] = rows[next];
            order[current].group_index = current;
            current = next;
        }
        rows[current] = saved;
        order[current].group_index = current;
    }
}

/* Explicit Sort As values bypass article removal. */
static inline uint32_t album_artist_skip_article(const uint16_t **text, uint32_t units)
{
    while (units && **text <= ' ') {
        (*text)++;
        units--;
    }
    const uint16_t *p = *text;
    uint32_t skip = 0;
    if (units > 4 && (p[0] | 32) == 't' && (p[1] | 32) == 'h' && (p[2] | 32) == 'e' &&
        p[3] == ' ')
        skip = 4;
    else if (units > 3 && (p[0] | 32) == 'a' && (p[1] | 32) == 'n' && p[2] == ' ')
        skip = 3;
    else if (units > 2 && (p[0] | 32) == 'a' && p[1] == ' ')
        skip = 2;
    *text += skip;
    return units - skip;
}

static inline int album_artist_valid_jumps(const uint32_t *jump, uint32_t tracks)
{
    if (!jump || !jump[0] || jump[0] > tracks || jump[2] != 0)
        return 0;
    for (uint32_t i = 0; i < jump[0]; i++)
        if (jump[i * 2 + 2] >= tracks || (i && jump[i * 2 + 2] <= jump[i * 2]))
            return 0;
    return 1;
}

static inline uint16_t album_artist_jump_letter(const uint32_t *jump, uint32_t index)
{
    uint32_t low = 0, high = jump[0];
    while (low + 1 < high) {
        uint32_t middle = low + (high - low) / 2;
        if (jump[middle * 2 + 2] <= index)
            low = middle;
        else
            high = middle;
    }
    return (uint16_t)jump[low * 2 + 1];
}
#endif
