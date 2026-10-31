/* SPDX-License-Identifier: GPL-3.0-only */

#include "song_info.h"

#include "album_artist.h"
#include "resources.h"
#include "song_info_ui.h"

/* UI actions run sequentially; each completed row is copied into the resource bank. */
static char body[1024];

static void write_field(uint32_t resource, uint32_t format, const char *value)
{
    unsigned size = cfw_format(body, sizeof(body), format, value);
    osos_resource_set(osos_resource_bank(), 0x53747220, resource, body, size + 1);
}

static void text_field(struct osos_media_item *item, uint32_t resource,
                        uint32_t format, enum osos_media_text field)
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
    write_field(resource, format, text && *text ? text : cfw_string(CFW_SONG_UNKNOWN));
    osos_native_string_destroy(&string);
}

static void number_field(uint32_t resource, uint32_t format, uint32_t number)
{
    char value[11];
    cfw_format(value, sizeof(value), CFW_SONG_NUMBER, number);
    write_field(resource, format, number ? value : cfw_string(CFW_SONG_UNKNOWN));
}

static void duration(uint32_t milliseconds)
{
    char value[32];
    if (!milliseconds) {
        write_field(CFW_INFO_DURATION, CFW_SONG_DURATION_FORMAT, cfw_string(CFW_SONG_UNKNOWN));
        return;
    }
    uint32_t seconds = milliseconds / 1000;
    uint32_t minutes = seconds / 60 % 60;
    uint32_t remainder = seconds % 60;
    if (seconds >= 3600) {
        cfw_format(value, sizeof(value), CFW_SONG_TIME_HOURS, seconds / 3600,
                   minutes < 10 ? "0" : "", minutes, remainder < 10 ? "0" : "", remainder);
    } else {
        cfw_format(value, sizeof(value), CFW_SONG_TIME, seconds / 60,
                   remainder < 10 ? "0" : "", remainder);
    }
    write_field(CFW_INFO_DURATION, CFW_SONG_DURATION_FORMAT, value);
}

static void ordinal(uint32_t resource, uint32_t format, uint32_t number, uint16_t total)
{
    char value[32];
    if (number && total >= number) {
        cfw_format(value, sizeof(value), CFW_SONG_ORDINAL, number, (uint32_t)total);
        write_field(resource, format, value);
    } else {
        number_field(resource, format, number);
    }
}

static void metadata(struct osos_media_item *item)
{
    text_field(item, CFW_INFO_TITLE, CFW_SONG_TITLE_FORMAT, OSOS_MEDIA_TITLE);
    text_field(item, CFW_INFO_ARTIST, CFW_SONG_ARTIST_FORMAT, OSOS_MEDIA_ARTIST);
    text_field(item, CFW_INFO_ALBUM, CFW_SONG_ALBUM_FORMAT, OSOS_MEDIA_ALBUM);
    text_field(item, CFW_INFO_ALBUM_ARTIST, CFW_SONG_ALBUM_ARTIST_FORMAT, 0);
    number_field(CFW_INFO_YEAR, CFW_SONG_YEAR_FORMAT, osos_media_item_year(item));
    text_field(item, CFW_INFO_GENRE, CFW_SONG_GENRE_FORMAT, OSOS_MEDIA_GENRE);
    text_field(item, CFW_INFO_COMPOSER, CFW_SONG_COMPOSER_FORMAT, OSOS_MEDIA_COMPOSER);
    text_field(item, CFW_INFO_GROUPING, CFW_SONG_GROUPING_FORMAT, OSOS_MEDIA_GROUPING);
    duration(osos_media_item_duration(item));
    /* Song records keep track/disc totals beside their 16-bit ordinals. */
    const uint8_t *record = (const uint8_t *)item->record;
    ordinal(CFW_INFO_TRACK, CFW_SONG_TRACK_FORMAT, osos_media_item_track(item), *(const uint16_t *)(record + 0x86));
    ordinal(CFW_INFO_DISC, CFW_SONG_DISC_FORMAT, osos_media_item_disc(item), *(const uint16_t *)(record + 0x8a));
    char value[32];
    uint32_t bitrate = osos_media_item_bitrate(item);
    cfw_format(value, sizeof(value), CFW_SONG_KBPS, bitrate);
    write_field(CFW_INFO_BITRATE, CFW_SONG_BITRATE_FORMAT,
                bitrate ? value : cfw_string(CFW_SONG_UNKNOWN));
    uint32_t bytes = osos_media_item_file_size(item);
    if (bytes) {
        cfw_format(value, sizeof(value), CFW_SONG_SIZE,
                   bytes / 1048576, bytes % 1048576 * 10 / 1048576);
    }
    write_field(CFW_INFO_FILE_SIZE, CFW_SONG_FILE_SIZE_FORMAT,
                bytes ? value : cfw_string(CFW_SONG_UNKNOWN));
    int32_t rating = osos_media_item_rating(item);
    if (rating > 0 && rating <= 100) {
        cfw_format(value, sizeof(value), CFW_SONG_STARS, (uint32_t)rating / 20);
    }
    write_field(CFW_INFO_RATING, CFW_SONG_RATING_FORMAT,
                rating > 0 && rating <= 100 ? value : cfw_string(CFW_SONG_UNRATED));
    cfw_format(value, sizeof(value), CFW_SONG_NUMBER, osos_media_item_play_count(item));
    write_field(CFW_INFO_PLAY_COUNT, CFW_SONG_PLAY_COUNT_FORMAT, value);
    cfw_format(value, sizeof(value), CFW_SONG_NUMBER, osos_media_item_skip_count(item));
    write_field(CFW_INFO_SKIP_COUNT, CFW_SONG_SKIP_COUNT_FORMAT, value);
}

void cfw_song_info_show(void *controller, struct osos_media_item *item)
{
    if (!item || !item->record || !osos_media_item_valid(item))
        return;
    metadata(item);

    void *screen =
        osos_create_screen_controller(osos_controller_registry(), CFW_SongInfo_Screen,
                                      CFW_SongInfo_Layout, 2); /* foregroundpush */
    osos_controller_push(controller, screen);
}
