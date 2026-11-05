/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"
#include "patch.h"
#include "native/media_progress.h"

enum {
    icon_width = 13,
    icon_height = 29,
    palette_capacity = 70,
    output_pixels = 32 + palette_capacity * 4,
    output_size = output_pixels + icon_width * icon_height,
};

/* Drawables borrow permanent, word-aligned BMaps for each icon variant. */
static uint32_t half_icons[4][(output_size + 3) / 4];
static const uint16_t icon_sizes[] = {537, 597, 533, 593};
_Static_assert(sizeof half_icons[0] >= output_size, "Half-circle bitmap capacity");

/* Inner semicircle coverage, out of 64 samples per pixel. Rows start at y=0
 * for video and y=10 for audio; columns start at x=6. The 23 partial pixels
 * require at most 23 additional palette entries. */
static const uint8_t inner_coverage[9][5] = {
    {12, 11, 0, 0, 0},    {48, 64, 49, 6, 0},   {48, 64, 64, 52, 1},
    {48, 64, 64, 64, 19}, {48, 64, 64, 64, 32}, {48, 64, 64, 64, 26},
    {48, 64, 64, 61, 6},  {48, 64, 63, 21, 0},  {30, 35, 10, 0, 0},
};

static uint32_t icon_word(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Derive from the current themed pixels. Only the interior of the right half
 * changes; the native antialiased outline and the video glyph stay intact. */
static const void *half_icon(unsigned variant)
{
    const uint8_t *source =
        (const void *)osos_ui_bitmap(OSOS_MEDIA_STATUS_BITMAP + variant);
    unsigned size = icon_sizes[variant];
    if (!source || source[0] != 0x64 || source[1] || source[4] != 13 || source[5] ||
        source[6] != 8 || source[7] || icon_word(source + 16) != 29 ||
        icon_word(source + 20) != 13 || icon_word(source + 24) != size - 28)
        return NULL;
    unsigned colors = icon_word(source + 28);
    unsigned pixels = 32 + colors * 4;
    if (colors > 256 || pixels + 13 * 29 != size)
        return NULL;
    unsigned transparent = 0;
    while (transparent < colors && source[32 + transparent * 4 + 3])
        transparent++;
    if (transparent == colors)
        return NULL;

    /* Validate before modifying storage that an earlier drawable may borrow.
     * Each partially covered pixel can add at most one palette entry; fully
     * cleared pixels use the existing transparent entry. */
    unsigned extra = 0;
    for (unsigned y = 0; y < 9; y++)
        for (unsigned x = 0; x < 5; x++)
            extra += inner_coverage[y][x] > 0 && inner_coverage[y][x] < 64;
    if (colors + extra > palette_capacity)
        return NULL;
    for (unsigned i = 0; i < icon_width * icon_height; i++)
        if (source[pixels + i] >= colors)
            return NULL;

    uint8_t *image = (void *)half_icons[variant];
    for (unsigned i = 0; i < output_pixels; i++)
        image[i] = i < pixels ? source[i] : 0;
    for (unsigned i = 0; i < icon_width * icon_height; i++)
        image[output_pixels + i] = source[pixels + i];
    half_icons[variant][6] = output_size - 28;
    half_icons[variant][7] = palette_capacity;

    unsigned top = variant < 2 ? 0 : 10;
    unsigned used = colors;
    for (unsigned y = 0; y < 9; y++) {
        for (unsigned x = 0; x < 5; x++) {
            unsigned clear = inner_coverage[y][x];
            if (!clear)
                continue;
            unsigned pixel = output_pixels + (top + y) * icon_width + x + 6;
            const uint8_t *color = source + 32 + image[pixel] * 4;
            unsigned alpha = (color[3] * (64 - clear) + 32) / 64;
            if (!alpha) {
                image[pixel] = transparent;
                continue;
            }
            unsigned match = 0;
            for (; match < used; match++) {
                const uint8_t *candidate = image + 32 + match * 4;
                if (candidate[0] == color[0] && candidate[1] == color[1] &&
                    candidate[2] == color[2] && candidate[3] == alpha)
                    break;
            }
            if (match == used) {
                uint8_t *added = image + 32 + used++ * 4;
                for (unsigned i = 0; i < 3; i++)
                    added[i] = color[i];
                added[3] = alpha;
            }
            image[pixel] = match;
        }
    }
    return image;
}

static int media_in_progress(struct osos_media_item *item, struct osos_player *player)
{
    if (!item->record || !osos_media_item_valid(item) ||
        !osos_media_item_remembers_position(item))
        return 0;
    uint32_t start = osos_media_item_start_time(item);
    uint32_t end = osos_media_item_stop_time(item);
    if (!end)
        end = osos_media_item_duration(item);
    if (end <= start)
        return 0;
    uint32_t position = osos_media_item_bookmark(item);
    if (player->current) {
        struct osos_media_item current;
        osos_media_item_init(&current);
        osos_player_current_item(player, &current);
        /* Record addresses may be reused after a database reload. */
        if (osos_media_item_valid(&current) && current.record == item->record &&
            current.reserved[1] == item->reserved[1])
            position = osos_media_progress_player_position(player);
    }
    /* Apple resets the bookmark to start on completion. A later unfinished
     * replay takes precedence over any previous play count. */
    return position > start && position < end;
}

PATCH_ARM int cfw_media_progress_property(void *model, uint32_t type, uint32_t property,
                                          int row, uintptr_t *out)
{
    int handled = osos_media_progress_icon_property(model, type, property, row, out);
    if (!handled || type != OSOS_RESOURCE_DRAW || row < 0 ||
        (property != 0x7835 && property != 0x7836 && property != 0x783c &&
         property != 0x783d))
        return handled;
    /* The native renderer inserts an All row before indexing the provider. */
    if (((uint8_t *)model)[OSOS_MEDIA_LIST_ALL_OFFSET])
        row--;
    if (row < 0)
        return handled;

    struct osos_player *player = osos_player_get();
    if (!player)
        return handled;
    struct osos_media_item item;
    osos_media_item_init(&item);
    osos_player_lock(player);
    osos_media_progress_icon_item(model, row, &item);
    int partial = media_in_progress(&item, player);
    int video = partial && osos_media_item_has_kind_8062(&item);
    osos_player_unlock(player);
    if (!partial)
        return handled;

    int selected = property == 0x7836 || property == 0x783d;
    unsigned variant = (video ? 0 : 2) + (selected ? 0 : 1);
    const void *bitmap = half_icon(variant);
    if (bitmap) {
        void *drawable =
            (uint8_t *)model + (selected ? OSOS_MEDIA_LIST_SELECTED_ICON_OFFSET
                                         : OSOS_MEDIA_LIST_ICON_OFFSET);
        osos_media_progress_icon_set(drawable, bitmap);
        *out = (uintptr_t)drawable;
    }
    return handled;
}
