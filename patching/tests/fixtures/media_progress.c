/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../../payload/native/bindings.h"

struct osos_media_item;
struct osos_media_record;
struct osos_player;
static int original(void *, uint32_t, uint32_t, int, uintptr_t *);
static void *model_ref(void *);
static struct osos_player *get_player(void);
static void lock(void *);
static void unlock(void *);
static int valid(struct osos_media_item *);
static struct osos_media_item *item_init(struct osos_media_item *,
                                         struct osos_media_record *, uint8_t);
static uint32_t player_position(struct osos_player *);
static int video_item(struct osos_media_item *);
static void *bank(void);
static uintptr_t bitmap(void *, uint32_t);
static void drawable_set(void *, const void *, uint32_t, uint32_t);
#undef OSOS_MEDIA_LIST_PROPERTY
#define OSOS_MEDIA_LIST_PROPERTY ((uintptr_t)original)
#undef OSOS_MODEL_REF_GET
#define OSOS_MODEL_REF_GET ((uintptr_t)model_ref)
#undef OSOS_PLAYER_GET
#define OSOS_PLAYER_GET ((uintptr_t)get_player)
#undef OSOS_PLAYER_LOCK
#define OSOS_PLAYER_LOCK ((uintptr_t)lock)
#undef OSOS_PLAYER_UNLOCK
#define OSOS_PLAYER_UNLOCK ((uintptr_t)unlock)
#undef OSOS_MEDIA_ITEM_VALID
#define OSOS_MEDIA_ITEM_VALID ((uintptr_t)valid)
#undef OSOS_MEDIA_ITEM_INIT
#define OSOS_MEDIA_ITEM_INIT ((uintptr_t)item_init)
#undef OSOS_PLAYER_POSITION
#define OSOS_PLAYER_POSITION ((uintptr_t)player_position)
#undef OSOS_MEDIA_ITEM_HAS_KIND_8062
#define OSOS_MEDIA_ITEM_HAS_KIND_8062 ((uintptr_t)video_item)
#undef OSOS_MENU_ITEM_CHANGED
#define OSOS_MENU_ITEM_CHANGED ((uintptr_t)bank)
#undef OSOS_RESOURCE_BITMAP
#define OSOS_RESOURCE_BITMAP ((uintptr_t)bitmap)
#undef OSOS_BITMAP_DRAWABLE_SET
#define OSOS_BITMAP_DRAWABLE_SET ((uintptr_t)drawable_set)
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert
#define CFW_PATCH_H
#define PATCH_ARM
#include "../../../payload/media_progress.c"

static uintptr_t item_methods[0x200 / 4], player_methods[0x240 / 4];
static uintptr_t model_methods[0x240 / 4];
static uintptr_t *source_vtable = model_methods;
static uint32_t record[64], other_record[64];
static struct osos_player player;
static struct osos_media_item episode, current_item;
static uint32_t renderer[0x500 / 4];
static uint8_t images[4][600];
static uint32_t start_time, stop_time, duration, bookmark, live;
static int locked, is_valid = 1, original_calls, item_calls, writes;
static int native_result = 1, have_player = 1;
static const void *drawn;
static void *drawn_at;

static int original(void *m, uint32_t t, uint32_t p, int r, uintptr_t *out)
{
    (void)t;
    (void)p;
    (void)r;
    assert(m == renderer);
    assert(!locked);
    original_calls++;
    *out = 1234;
    return native_result;
}

static void *model_ref(void *ref)
{
    assert(ref == (uint8_t *)renderer + 0x18);
    return &source_vtable;
}

static void get_item(struct osos_media_item *out, void *source, int row)
{
    assert(source == &source_vtable);
    assert(locked && row == 2);
    item_calls++;
    *out = episode;
}

static struct osos_player *get_player(void)
{
    return have_player ? &player : NULL;
}

static void lock(void *mutex)
{
    (void)mutex;
    assert(!locked);
    locked = 1;
}

static void unlock(void *mutex)
{
    (void)mutex;
    assert(locked);
    locked = 0;
}

static int valid(struct osos_media_item *item)
{
    return is_valid && item->record;
}

static struct osos_media_item *item_init(struct osos_media_item *item,
                                         struct osos_media_record *r, uint8_t flags)
{
    (void)r;
    (void)flags;
    memset(item, 0, sizeof(*item));
    item->vtable = item_methods;
    return item;
}

static uint32_t get_remembers(struct osos_media_item *item)
{
    return (((uint8_t *)item->record)[0x8f] & 4) != 0;
}

static uint32_t get_start(struct osos_media_item *item)
{
    (void)item;
    return start_time;
}

static uint32_t get_stop(struct osos_media_item *item)
{
    (void)item;
    return stop_time;
}

static uint32_t get_duration(struct osos_media_item *item)
{
    (void)item;
    return duration;
}

static uint32_t get_bookmark(struct osos_media_item *item)
{
    (void)item;
    return bookmark;
}

static uint32_t player_position(struct osos_player *p)
{
    assert(p == &player && locked);
    return live;
}

static void get_current(struct osos_player *p, struct osos_media_item *item)
{
    assert(p == &player && locked);
    *item = current_item;
}

static int video_item(struct osos_media_item *item)
{
    return (((uint32_t *)item->record)[0xbc / 4] & 0x8062) != 0;
}

static void *bank(void)
{
    return images;
}

static uintptr_t bitmap(void *b, uint32_t id)
{
    assert(b == images && id >= 0x0dad0714 && id <= 0x0dad0717);
    return (uintptr_t)images[id - 0x0dad0714];
}

static void drawable_set(void *d, const void *b, uint32_t x, uint32_t y)
{
    assert(!x && !y && !locked);
    drawn_at = d;
    drawn = b;
    writes++;
}

static void put_word(uint8_t *p, uint32_t n)
{
    for (int i = 0; i < 4; i++)
        p[i] = n >> (8 * i);
}

static int render(uint32_t property)
{
    uintptr_t out;
    drawn = NULL;
    assert(
        cfw_media_progress_property(renderer, OSOS_RESOURCE_DRAW, property, 2, &out));
    assert(!locked);
    assert(drawn ? out == (uintptr_t)drawn_at : out == 1234);
    return drawn != NULL;
}

/* Check decoded pixels against the semicircle geometry. */
static void check_pixels(unsigned variant)
{
    const uint8_t *source = images[variant];
    const uint8_t *result = half_icon(variant);
    assert(result);
    unsigned input = 32 + icon_word(source + 28) * 4;
    unsigned output = 32 + icon_word(result + 28) * 4;
    int center_y = variant < 2 ? 67 : 227;
    for (int y = 0; y < 29; y++) {
        for (int x = 0; x < 13; x++) {
            unsigned clear = 0;
            if (x >= 6 && x < 11) {
                for (int sy = 0; sy < 8; sy++) {
                    int dy = y * 16 + sy * 2 - 7 - center_y;
                    for (int sx = 0; sx < 8; sx++) {
                        int dx = x * 16 + sx * 2 - 7 - 96;
                        clear += dx >= -4 && dx * dx + dy * dy < 64 * 64;
                    }
                }
            }
            const uint8_t *before = source + 32 + source[input + y * 13 + x] * 4;
            const uint8_t *after = result + 32 + result[output + y * 13 + x] * 4;
            unsigned alpha = (before[3] * (64 - clear) + 32) / 64;
            assert(after[3] == alpha);
            if (alpha)
                assert(!memcmp(before, after, 3));
        }
    }
}

static void check_bitmap_failures(void)
{
    uint8_t saved[sizeof half_icons[3]];
    assert(half_icon(3));
    memcpy(saved, half_icons[3], sizeof saved);
    uint8_t *source = images[3];
    unsigned colors = icon_word(source + 28);
    unsigned pixels = 32 + colors * 4;
    /* A late invalid index must not alter storage borrowed by an earlier row. */
    uint8_t last = source[pixels + 13 * 29 - 1];
    source[pixels + 13 * 29 - 1] = colors;
    assert(!render(0x7835));
    assert(!memcmp(saved, half_icons[3], sizeof saved));
    source[pixels + 13 * 29 - 1] = last;
    source[35] = 255;
    assert(!render(0x7835));
    assert(!memcmp(saved, half_icons[3], sizeof saved));
    source[35] = 0;
    put_word(source + 28, 256);
    assert(!render(0x7835));
    assert(!memcmp(saved, half_icons[3], sizeof saved));
    put_word(source + 28, 0x40000000u + colors);
    assert(!render(0x7835));
    assert(!memcmp(saved, half_icons[3], sizeof saved));
    put_word(source + 28, colors);
    assert(render(0x7835));
}

int main(void)
{
    model_methods[0x22c / 4] = (uintptr_t)get_item;
    item_methods[0xe8 / 4] = (uintptr_t)get_remembers;
    item_methods[0x90 / 4] = (uintptr_t)get_bookmark;
    item_methods[0x94 / 4] = (uintptr_t)get_start;
    item_methods[0x98 / 4] = (uintptr_t)get_stop;
    item_methods[0xa0 / 4] = (uintptr_t)get_duration;
    player_methods[OSOS_PLAYER_CURRENT_ITEM_SLOT / 4] = (uintptr_t)get_current;
    player.vtable = player_methods;
    item_init(&episode, NULL, 0);
    episode.record = (void *)record;
    episode.reserved[1] = 7;
    record[0xbc / 4] = 4;
    ((uint8_t *)record)[0x8f] = 4;
    duration = 3600000;
    const uint16_t native_sizes[] = {537, 597, 533, 593};
    for (unsigned i = 0; i < 4; i++) {
        uint8_t *b = images[i];
        b[0] = 0x64;
        b[4] = 13;
        b[6] = 8;
        put_word(b + 16, 29);
        put_word(b + 20, 13);
        put_word(b + 24, native_sizes[i] - 28);
        unsigned colors = (native_sizes[i] - 32 - 13 * 29) / 4;
        put_word(b + 28, colors);
        for (unsigned j = 1; j < colors; j++)
            b[32 + j * 4 + 3] = 255;
        memset(b + 32 + colors * 4, 1, 13 * 29);
    }
    assert(!render(0x7835)); /* unplayed */
    bookmark = 60000;
    check_bitmap_failures();
    assert(render(0x7835));
    assert(drawn_at == (uint8_t *)renderer + 0x258);
    const uint8_t *normal = drawn;
    unsigned px = 32 + icon_word(normal + 28) * 4;
    assert(normal[px + 14 * 13 + 7] == 0);
    assert(normal[px + 14 * 13 + 3] == 1);
    assert(normal[px + 14 * 13 + 11] == 1);
    unsigned edge_color = normal[px + 14 * 13 + 6];
    /* Straight inner edge retains quarter coverage. */
    assert(normal[32 + edge_color * 4 + 3] == 64);
    edge_color = normal[px + 11 * 13 + 8];
    assert(normal[32 + edge_color * 4 + 3] > 0);
    assert(normal[32 + edge_color * 4 + 3] < 255); /* Smooth curved inner edge. */
    assert(render(0x7836));
    assert(drawn_at == (uint8_t *)renderer + 0x310 && drawn != normal);
    assert(render(0x783c) && render(0x783d));
    record[0xbc / 4] = 6;
    assert(render(0x7835));
    const uint8_t *v = drawn;
    px = 32 + icon_word(v + 28) * 4;
    assert(v[px + 4 * 13 + 7] == 0);
    unsigned video_pixels = 32 + icon_word(images[1] + 28) * 4;
    assert(!memcmp(v + px + 16 * 13, images[1] + video_pixels + 16 * 13, 13 * 13));
    record[0xbc / 4] = 4;
    /* Theme changes must update colors from the native asset. */
    images[3][36] = 17;
    assert(render(0x7835) && ((const uint8_t *)drawn)[36] == 17);
    images[3][0] = 0;
    assert(!render(0x7835));
    images[3][0] = 0x64;
    bookmark = 0;
    ((uint8_t *)record)[0x8f] |= 8; /* Native played flag. */
    assert(!render(0x7835));
    bookmark = 60000;
    assert(render(0x7835));
    bookmark = duration;
    assert(!render(0x7835));
    bookmark = UINT32_MAX;
    assert(!render(0x7835));
    start_time = 30000;
    stop_time = 120000;
    bookmark = start_time;
    assert(!render(0x7835));
    bookmark++;
    assert(render(0x7835));
    bookmark = stop_time;
    assert(!render(0x7835));
    stop_time = start_time;
    assert(!render(0x7835));
    start_time = stop_time = 0;
    duration = 0;
    assert(!render(0x7835));
    duration = 3600000;
    bookmark = 60000;
    current_item = episode;
    player.current = &current_item;
    live = 0;
    assert(!render(0x7835)); /* Live position supersedes saved bookmark. */
    live = 90000;
    assert(render(0x7835));
    live = duration;
    assert(!render(0x7835));
    current_item.reserved[1]++;
    assert(render(0x7835)); /* Different generation cannot supply position. */
    current_item.reserved[1] = episode.reserved[1];
    current_item.record = (void *)other_record;
    assert(render(0x7835));
    is_valid = 0;
    assert(!render(0x7835));
    is_valid = 1;

    /* Eligibility follows resume state; media kind selects only the glyph. */
    const struct {
        uint32_t kind;
        unsigned variant;
    } media[] = {
        {1, 3},        /* Music */
        {8, 3},        /* Audiobook */
        {4, 3},        /* Podcast */
        {6, 1},        /* Video podcast */
        {0x200001, 3}, /* Audio iTunes U */
        {0x200002, 1}, /* Video iTunes U */
        {2, 1},        /* Movie */
        {32, 1},       /* Music video */
        {64, 1},       /* TV */
    };

    for (unsigned i = 0; i < sizeof media / sizeof media[0]; i++) {
        record[0xbc / 4] = media[i].kind;
        assert(render(0x7835) && drawn == half_icons[media[i].variant]);
        assert(render(0x783c) && drawn == half_icons[media[i].variant]);
    }
    ((uint8_t *)record)[0x8f] &= ~4;
    assert(!render(0x7835));
    ((uint8_t *)record)[0x8f] |= 4;
    player.current = NULL;
    bookmark = 60000;
    ((uint8_t *)renderer)[0x25] = 1;
    uintptr_t all_out;
    int all_before = item_calls;
    assert(
        cfw_media_progress_property(renderer, OSOS_RESOURCE_DRAW, 0x7835, 0, &all_out));
    assert(all_out == 1234 && item_calls == all_before);
    assert(
        cfw_media_progress_property(renderer, OSOS_RESOURCE_DRAW, 0x7835, 3, &all_out));
    assert(all_out == (uintptr_t)drawn_at && item_calls == all_before + 1);
    ((uint8_t *)renderer)[0x25] = 0;
    record[0xbc / 4] = 4;
    episode.record = NULL;
    assert(!render(0x7835));
    episode.record = (void *)record;
    int before = item_calls;
    assert(!render(0x7833)); /* Aggregate show row. */
    assert(item_calls == before);
    uintptr_t out;
    assert(cfw_media_progress_property(renderer, OSOS_RESOURCE_DRAW, 0x7835, -1, &out));
    assert(cfw_media_progress_property(renderer, 0, 0x7835, 2, &out));
    assert(item_calls == before);
    native_result = 0;
    assert(!cfw_media_progress_property(renderer, OSOS_RESOURCE_DRAW, 0x7835, 2, &out));
    assert(item_calls == before);
    native_result = 1;
    have_player = 0;
    assert(!render(0x7835));
    assert(original_calls > writes && writes > 0 && !locked);
    for (unsigned variant = 0; variant < 4; variant++) {
        uint8_t *b = images[variant];
        unsigned colors = icon_word(b + 28);
        unsigned pixels = 32 + colors * 4;
        for (unsigned j = 1; j < colors; j++) {
            b[32 + j * 4] = j * 5;
            b[33 + j * 4] = 255 - j * 3;
            b[34 + j * 4] = j * 7;
            b[35 + j * 4] = j * 5;
        }
        for (unsigned j = 0; j < 13 * 29; j++)
            b[pixels + j] = j % colors;
        check_pixels(variant);
    }
    /* Distinct opaque source colors exercise all 70 palette entries. */
    uint8_t *worst = images[1];
    unsigned colors = icon_word(worst + 28);
    unsigned pixels = 32 + colors * 4;
    for (unsigned j = 1; j < colors; j++)
        worst[35 + j * 4] = 255;
    unsigned next = 1;
    for (unsigned y = 0; y < 9; y++)
        for (unsigned x = 0; x < 5; x++)
            if (inner_coverage[y][x] > 0 && inner_coverage[y][x] < 64)
                worst[pixels + y * 13 + x + 6] = next++;
    check_pixels(1);
    const uint8_t *full = (const void *)half_icons[1];
    unsigned full_pixels = 32 + icon_word(full + 28) * 4;
    assert(memchr(full + full_pixels, 69, 13 * 29));
    return 0;
}
