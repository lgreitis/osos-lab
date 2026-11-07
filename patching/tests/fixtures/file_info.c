/* SPDX-License-Identifier: GPL-3.0-only */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CFW_OSOS_H
#include "file_info_ui.h"

enum {
    OSOS_PROPERTY_STRING = 0x53747220,
    OSOS_PROPERTY_COUNT = 0x564d6178,
};

struct osos_media_record;

struct osos_media_item {
    void *record;
};

struct osos_native_string {
    const char *data;
};

enum osos_media_text {
    OSOS_MEDIA_TITLE = 0x50,
    OSOS_MEDIA_ALBUM = 0x54,
    OSOS_MEDIA_ARTIST = 0x58,
    OSOS_MEDIA_GENRE = 0x60,
    OSOS_MEDIA_COMPOSER = 0x64,
    OSOS_MEDIA_GROUPING = 0x68,
};

static int valid = 1, create_failed, controller, screen, registry;
static unsigned creates, pushes, strings;
static uint8_t record[0x100];
static const char *path = "F21\\FMTAAC.m4a";

static struct {
    uint32_t year, duration, bitrate, file_size, play_count, skip_count, chapters;
    uint16_t track, disc;
    int32_t rating;
} fake;

static int osos_media_item_valid(struct osos_media_item *item)
{
    return valid && item->record;
}

static void osos_native_string_init(struct osos_native_string *s)
{
    s->data = "";
    strings++;
}

static void osos_native_string_destroy(struct osos_native_string *s)
{
    (void)s;
    assert(strings);
    strings--;
}

static const char *osos_native_string_data(struct osos_native_string *s)
{
    return s->data;
}

static void osos_native_string_assign_utf16(struct osos_native_string *s,
                                            const uint16_t *text, uint32_t units)
{
    assert(text);
    s->data = units ? "Album Artist Name" : "";
}

uint32_t cfw_album_artist_read(struct osos_media_record *r, uint16_t *out,
                               uint32_t capacity)
{
    assert(r && out && capacity == 255);
    out[0] = 'A';
    return 1;
}

static void osos_media_item_text(struct osos_media_item *item,
                                 enum osos_media_text field,
                                 struct osos_native_string *s)
{
    assert(item->record);
    switch (field) {
    case OSOS_MEDIA_TITLE:
        s->data = "Song Title";
        break;
    case OSOS_MEDIA_ARTIST:
        s->data = "Ärtist & Co, 日本";
        break;
    case OSOS_MEDIA_ALBUM:
        s->data = "Album Name";
        break;
    default:
        s->data = "";
    }
}

/* The native getter constructs the returned string. */
static void osos_media_item_path(struct osos_media_item *item,
                                 struct osos_native_string *s)
{
    assert(item->record);
    s->data = path;
    strings++;
}

#define FIELD(name, type)                                                              \
    static type osos_media_item_##name(struct osos_media_item *item)                   \
    {                                                                                  \
        assert(item->record);                                                          \
        return fake.name;                                                              \
    }
FIELD(year, uint32_t)
FIELD(duration, uint32_t)
FIELD(track, uint16_t)
FIELD(disc, uint16_t)
FIELD(bitrate, uint32_t)
FIELD(file_size, uint32_t)
FIELD(play_count, uint32_t)
FIELD(skip_count, uint32_t)
FIELD(rating, int32_t)
#undef FIELD

static uint32_t osos_media_item_chapter_count(struct osos_media_item *item)
{
    assert(item->record);
    return fake.chapters;
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

static void set_kind(uint32_t kind)
{
    memcpy(record + 0xbc, &kind, sizeof(kind));
}

static void set_sample_rate(float hertz)
{
    memcpy(record + 0x74, &hertz, sizeof(hertz));
}

static uint32_t count(void)
{
    uintptr_t out = 0;
    assert(cfw_file_info_property(OSOS_PROPERTY_COUNT, CFW_FileInfo_Rows, &out));
    return (uint32_t)out;
}

static const char *label(uint32_t index)
{
    uintptr_t out = 0;
    assert(cfw_file_info_indexed_property(OSOS_PROPERTY_STRING, CFW_FileInfo_Rows,
                                          index, &out));
    return (const char *)out;
}

static const char *detail(uint32_t index)
{
    uintptr_t out = 0;
    assert(cfw_file_info_indexed_property(OSOS_PROPERTY_STRING, CFW_FileInfo_Detail,
                                          index, &out));
    /* A text value stays in its native string until the next request. */
    assert(strings <= 1);
    return (const char *)out;
}

static uint32_t row(const char *name)
{
    for (uint32_t i = 0; i < count(); i++)
        if (!strcmp(label(i), name))
            return i;
    assert(!"missing row");
    return 0;
}

static int has_row(const char *name)
{
    for (uint32_t i = 0; i < count(); i++)
        if (!strcmp(label(i), name))
            return 1;
    return 0;
}

static void open(uint32_t kind)
{
    struct osos_media_item item = {record};
    set_kind(kind);
    assert(cfw_file_info_show(&controller, &item));
}

static void check_song(void)
{
    open(1);
    assert(count() == 18);
    assert(!strcmp(label(0), "Title") && !strcmp(detail(0), "Song Title"));
    assert(!strcmp(detail(row("Artist")), "Ärtist & Co, 日本"));
    assert(!strcmp(detail(row("Album Artist")), "Album Artist Name"));
    assert(!strcmp(detail(row("Composer")), "Unknown"));
    assert(!strcmp(detail(row("Year")), "Unknown"));
    fake.year = 2026;
    assert(!strcmp(detail(row("Year")), "2026"));
    fake.duration = 125000;
    assert(!strcmp(detail(row("Duration")), "2:05"));
    fake.duration = 3725000;
    assert(!strcmp(detail(row("Duration")), "1:02:05"));
    fake.track = 3;
    assert(!strcmp(detail(row("Track")), "3"));
    record[0x86] = 12;
    assert(!strcmp(detail(row("Track")), "3 of 12"));
    assert(!strcmp(detail(row("Disc")), "Unknown"));
    assert(!strcmp(detail(row("File Type")), "M4A"));
    path = "F22\\BOOK.m4b";
    assert(!strcmp(detail(row("File Type")), "M4B"));
    path = "F22\\NOEXT";
    assert(!strcmp(detail(row("File Type")), "Unknown"));
    path = "F2.2\\X.toolongext";
    assert(!strcmp(detail(row("File Type")), "Unknown"));
    assert(!strcmp(detail(row("Sample Rate")), "Unknown"));
    set_sample_rate(44100.0f);
    assert(!strcmp(detail(row("Sample Rate")), "44.1 kHz"));
    set_sample_rate(48000.0f);
    assert(!strcmp(detail(row("Sample Rate")), "48 kHz"));
    set_sample_rate(-44100.0f);
    assert(!strcmp(detail(row("Sample Rate")), "Unknown"));
    fake.bitrate = 256;
    assert(!strcmp(detail(row("Bitrate")), "256 kbps"));
    fake.file_size = 14747256;
    assert(!strcmp(detail(row("File Size")), "14.0 MiB"));
    assert(!strcmp(detail(row("Rating")), "Unrated"));
    fake.rating = 80;
    assert(!strcmp(detail(row("Rating")), "4/5"));
    assert(!strcmp(detail(row("Play Count")), "0"));
    fake.skip_count = 7;
    assert(!strcmp(detail(row("Skip Count")), "7"));
    assert(!has_row("Chapters"));
}

static void check_kinds(void)
{
    open(0x4); /* Podcast without chapters. */
    assert(count() == 10 && !has_row("Chapters") && has_row("Podcast"));
    assert(!strcmp(detail(row("Podcast")), "Album Name"));
    fake.chapters = 3;
    open(0x4);
    assert(count() == 11 && !strcmp(detail(row("Chapters")), "3"));
    open(0x8);
    assert(count() == 10 && has_row("Author") && has_row("Book"));
    assert(!has_row("Album") && !has_row("Skip Count"));
    open(0x200000);
    assert(count() == 11 && has_row("Album") && !has_row("Podcast"));
    fake.chapters = 0;
    open(0x20);
    assert(count() == 10 && has_row("Artist") && !has_row("Sample Rate"));
    open(0x2);
    assert(count() == 8 && !has_row("Artist") && has_row("File Size"));
    open(0x40);
    assert(count() == 8);
    open(0x8000);
    assert(count() == 8);
    open(0x8 | 0x4); /* An audiobook flag wins over podcast. */
    assert(has_row("Author"));
}

static void check_bounds(void)
{
    uintptr_t out = 0;
    open(1);
    assert(!cfw_file_info_indexed_property(OSOS_PROPERTY_STRING, CFW_FileInfo_Rows,
                                           count(), &out));
    assert(!cfw_file_info_indexed_property(OSOS_PROPERTY_COUNT, CFW_FileInfo_Rows, 0,
                                           &out));
    assert(!cfw_file_info_indexed_property(OSOS_PROPERTY_STRING, 0, 0, &out));
    assert(!cfw_file_info_property(OSOS_PROPERTY_STRING, CFW_FileInfo_Rows, &out));
    valid = 0; /* A library reload leaves labels and blank values. */
    assert(!strcmp(label(0), "Title") && !strcmp(detail(0), ""));
    valid = 1;
    assert(cfw_file_info_action("CFW_FileInfo_Select", 0));
    assert(!cfw_file_info_action("CFW_FileInfo", 0));
    assert(!cfw_file_info_action(NULL, 0));
}

int main(void)
{
    struct osos_media_item item = {NULL};
    assert(!cfw_file_info_show(&controller, NULL));
    assert(!cfw_file_info_show(&controller, &item));
    item.record = record;
    valid = 0;
    assert(!cfw_file_info_show(&controller, &item));
    assert(!creates && !pushes && !strings);
    valid = 1;
    create_failed = 1;
    assert(!cfw_file_info_show(&controller, &item));
    assert(creates == 1 && !pushes && !strings);
    create_failed = 0;
    check_song();
    check_kinds();
    check_bounds();
    assert(!strcmp(detail(0), "Song Title") && strings == 1);
    open(1); /* Opening releases the previous text value. */
    assert(!strings);
    assert(!strcmp(detail(row("Play Count")), "0") && !strings);
    assert(pushes == creates - 1);
    return 0;
}
