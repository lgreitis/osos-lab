/* SPDX-License-Identifier: GPL-3.0-only */

#include "song_info.h"

#include "album_artist.h"
#include "song_info_ui.h"

/* UI actions run sequentially; each completed row is copied into the resource bank. */
static struct {
    char data[1024];
    unsigned size;
    uint32_t resource;
} body;

static void append(const char *text)
{
    while (*text) {
        unsigned bytes = 1;
        unsigned char first = (unsigned char)*text;
        if (first >= 0xc0 && first < 0xe0)
            bytes = 2;
        else if (first >= 0xe0 && first < 0xf0)
            bytes = 3;
        else if (first >= 0xf0)
            bytes = 4;
        if (body.size + bytes >= sizeof(body.data))
            break;
        for (unsigned i = 0; i < bytes && *text; i++)
            body.data[body.size++] = *text++;
    }
    body.data[body.size] = 0;
}

static void decimal(uint32_t value)
{
    char digits[11];
    unsigned start = sizeof(digits) - 1;
    digits[start] = 0;
    do {
        digits[--start] = '0' + value % 10;
        value /= 10;
    } while (value);
    append(digits + start);
}

static void flush(void)
{
    if (body.resource)
        osos_resource_set(osos_resource_bank(), 0x53747220, body.resource,
                          body.data, body.size + 1);
}

static void label(uint32_t resource, const char *name)
{
    flush();
    body.resource = resource;
    body.size = 0;
    body.data[0] = 0;
    append(name);
    append(": ");
}

static void text_field(struct osos_media_item *item, uint32_t resource,
                        const char *name, enum osos_media_text field)
{
    struct osos_native_string string;
    osos_native_string_init(&string);
    if (field) {
        osos_media_item_text(item, field, &string);
    } else {
        uint16_t text[255];
        uint32_t units = cfw_album_artist_read(item->record, text, 255);
        osos_native_string_assign_utf16(&string, text, units);
    }
    const char *text = osos_native_string_data(&string);
    label(resource, name);
    append(text && *text ? text : "Unknown");
    osos_native_string_destroy(&string);
}

static void number_field(uint32_t resource, const char *name, uint32_t number, const char *unit)
{
    label(resource, name);
    if (!number) {
        append("Unknown");
        return;
    }
    decimal(number);
    append(unit);
}

static void two_digits(uint32_t number)
{
    if (number < 10)
        append("0");
    decimal(number);
}

static void duration(uint32_t milliseconds)
{
    label(CFW_INFO_DURATION, "Duration");
    if (!milliseconds) {
        append("Unknown");
        return;
    }
    uint32_t seconds = milliseconds / 1000;
    if (seconds >= 3600) {
        decimal(seconds / 3600);
        append(":");
        two_digits(seconds / 60 % 60);
    } else {
        decimal(seconds / 60);
    }
    append(":");
    two_digits(seconds % 60);
}

static void ordinal(uint32_t resource, const char *name, uint32_t number, uint16_t total)
{
    number_field(resource, name, number, "");
    if (number && total >= number) {
        append(" of ");
        decimal(total);
    }
}

static void metadata(struct osos_media_item *item)
{
    text_field(item, CFW_INFO_TITLE, "Title", OSOS_MEDIA_TITLE);
    text_field(item, CFW_INFO_ARTIST, "Artist", OSOS_MEDIA_ARTIST);
    text_field(item, CFW_INFO_ALBUM, "Album", OSOS_MEDIA_ALBUM);
    text_field(item, CFW_INFO_ALBUM_ARTIST, "Album Artist", 0);
    number_field(CFW_INFO_YEAR, "Year", osos_media_item_year(item), "");
    text_field(item, CFW_INFO_GENRE, "Genre", OSOS_MEDIA_GENRE);
    text_field(item, CFW_INFO_COMPOSER, "Composer", OSOS_MEDIA_COMPOSER);
    text_field(item, CFW_INFO_GROUPING, "Grouping", OSOS_MEDIA_GROUPING);
    duration(osos_media_item_duration(item));
    /* Song records keep track/disc totals beside their 16-bit ordinals. */
    const uint8_t *record = (const uint8_t *)item->record;
    ordinal(CFW_INFO_TRACK, "Track", osos_media_item_track(item), *(const uint16_t *)(record + 0x86));
    ordinal(CFW_INFO_DISC, "Disc", osos_media_item_disc(item), *(const uint16_t *)(record + 0x8a));
    number_field(CFW_INFO_BITRATE, "Bitrate", osos_media_item_bitrate(item), " kbps");
    label(CFW_INFO_FILE_SIZE, "File size");
    uint32_t bytes = osos_media_item_file_size(item);
    if (bytes) {
        decimal(bytes / 1048576);
        append(".");
        decimal(bytes % 1048576 * 10 / 1048576);
        append(" MiB");
    } else {
        append("Unknown");
    }
    label(CFW_INFO_RATING, "Rating");
    int32_t rating = osos_media_item_rating(item);
    if (rating > 0 && rating <= 100) {
        decimal(rating / 20);
        append("/5");
    } else {
        append("Unrated");
    }
    label(CFW_INFO_PLAY_COUNT, "Play count");
    decimal(osos_media_item_play_count(item));
    label(CFW_INFO_SKIP_COUNT, "Skip count");
    decimal(osos_media_item_skip_count(item));
}

void cfw_song_info_show(void *controller, struct osos_media_item *item)
{
    if (!item || !item->record || !osos_media_item_valid(item))
        return;
    body.size = 0;
    body.resource = 0;
    body.data[0] = 0;
    metadata(item);
    flush();

    typedef void *(*registry)(void);
    typedef void *(*create)(void *, uint32_t, uint32_t, uint32_t);
    typedef void (*push)(void *, void *);
    void *screen = ((create)OSOS_CREATE_SCREEN_CONTROLLER)(
        ((registry)OSOS_CONTROLLER_REGISTRY)(), CFW_SongInfo_Screen,
        CFW_SongInfo_Layout, 2); /* foregroundpush */
    ((push)osos_method(controller, 0xd0))(controller, screen);
}

