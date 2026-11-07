/* SPDX-License-Identifier: GPL-3.0-only */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#define CFW_OSOS_H
#include "file_info_ui.h"

struct osos_media_record;

struct osos_media_item {
    void *record;
};

struct osos_native_string {
    int unused;
};

enum osos_media_text {
    OSOS_MEDIA_TITLE = 1,
    OSOS_MEDIA_ARTIST,
    OSOS_MEDIA_ALBUM,
    OSOS_MEDIA_GENRE,
    OSOS_MEDIA_COMPOSER,
    OSOS_MEDIA_GROUPING
};

static int valid = 1, create_failed, controller, screen, registry, bank;
static unsigned creates, pushes, resources, strings;
static uint16_t record[128];

static int osos_media_item_valid(struct osos_media_item *item)
{
    return valid && item->record;
}

static void *osos_resource_bank(void)
{
    return &bank;
}

static void osos_resource_set(void *b, uint32_t kind, uint32_t id, const void *data,
                              unsigned size)
{
    assert(b == &bank && kind == 0x53747220 && id && data && size);
    resources++;
}

static void osos_native_string_init(struct osos_native_string *s)
{
    (void)s;
    strings++;
}

static void osos_native_string_destroy(struct osos_native_string *s)
{
    (void)s;
    assert(strings);
    strings--;
}

static void osos_media_item_text(struct osos_media_item *item,
                                 enum osos_media_text field,
                                 struct osos_native_string *s)
{
    (void)s;
    assert(item->record && field);
}

uint32_t cfw_album_artist_read(struct osos_media_record *r, uint16_t *out,
                               uint32_t capacity)
{
    assert(r && out && capacity);
    return 0;
}

static void osos_native_string_assign_utf16(struct osos_native_string *s,
                                            const uint16_t *text, uint32_t units)
{
    (void)s;
    assert(text && !units);
}

static const char *osos_native_string_data(struct osos_native_string *s)
{
    (void)s;
    return "Test";
}

#define FIELD(name)                                                                    \
    static uint32_t osos_media_item_##name(struct osos_media_item *item)               \
    {                                                                                  \
        assert(item->record);                                                          \
        return 0;                                                                      \
    }
FIELD(year)
FIELD(duration)
FIELD(track)
FIELD(disc)
FIELD(bitrate)
FIELD(file_size)
FIELD(play_count)
FIELD(skip_count)

static int32_t osos_media_item_rating(struct osos_media_item *item)
{
    assert(item->record);
    return 0;
}

static void *osos_controller_registry(void)
{
    return &registry;
}

static void *osos_create_screen_controller(void *r, uint32_t id, uint32_t layout,
                                           unsigned mode)
{
    assert(r == &registry && id == CFW_FileInfo_Screen);
    assert(layout == CFW_FileInfo_Layout && mode == 2);
    creates++;
    return create_failed ? NULL : &screen;
}

static void osos_controller_push(void *owner, void *destination)
{
    assert(owner == &controller && destination == &screen);
    pushes++;
}

#include "../../../payload/file_info.c"

int main(void)
{
    struct osos_media_item item = {NULL};
    assert(!cfw_file_info_show(&controller, NULL));
    assert(!cfw_file_info_show(&controller, &item));
    item.record = record;
    valid = 0;
    assert(!cfw_file_info_show(&controller, &item));
    assert(!creates && !pushes && !resources && !strings);
    valid = 1;
    create_failed = 1;
    assert(!cfw_file_info_show(&controller, &item));
    assert(creates == 1 && !pushes && resources && !strings);
    create_failed = 0;
    for (unsigned i = 0; i < 3; i++) {
        assert(cfw_file_info_show(&controller, &item));
        assert(creates == i + 2 && pushes == i + 1 && !strings);
    }
    return 0;
}
