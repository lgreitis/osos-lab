/* SPDX-License-Identifier: GPL-3.0-only */

#include "file_info.h"

#include "album_artist.h"
#include "cfw.h"
#include "file_info_ui.h"

enum field {
    TITLE,
    ARTIST,
    AUTHOR,
    ALBUM,
    PODCAST,
    BOOK,
    ALBUM_ARTIST,
    YEAR,
    GENRE,
    COMPOSER,
    GROUPING,
    DURATION,
    CHAPTERS,
    TRACK,
    DISC,
    FILE_TYPE,
    SAMPLE_RATE,
    BITRATE,
    FILE_SIZE,
    RATING,
    PLAY_COUNT,
    SKIP_COUNT,
    FIELDS
};

/* Labels in field order, each found by its offset into one string. */
static const char labels[] =
    "Title\0Artist\0Author\0Album\0Podcast\0Book\0Album Artist\0"
    "Year\0Genre\0Composer\0Grouping\0Duration\0Chapters\0Track\0"
    "Disc\0File Type\0Sample Rate\0Bitrate\0File Size\0Rating\0"
    "Play Count\0Skip Count";
static const uint8_t label_offsets[FIELDS] = {
    0,  6,  13, 20,  26,  34,  39,  52,  57,  63,  72,
    81, 90, 99, 105, 110, 120, 132, 140, 150, 157, 168,
};

/* Rows per media kind, in display order. CHAPTERS is shown only when nonzero. */
static const uint8_t song_rows[] = {
    TITLE,     ARTIST,   ALBUM,      ALBUM_ARTIST, YEAR,      GENRE,       COMPOSER,
    GROUPING,  DURATION, TRACK,      DISC,         FILE_TYPE, SAMPLE_RATE, BITRATE,
    FILE_SIZE, RATING,   PLAY_COUNT, SKIP_COUNT,   FIELDS,
};
static const uint8_t podcast_rows[] = {
    TITLE,     PODCAST,     ARTIST,  GENRE,     DURATION,   CHAPTERS,
    FILE_TYPE, SAMPLE_RATE, BITRATE, FILE_SIZE, PLAY_COUNT, FIELDS,
};
static const uint8_t lecture_rows[] = {
    TITLE,     ALBUM,       ARTIST,  GENRE,     DURATION,   CHAPTERS,
    FILE_TYPE, SAMPLE_RATE, BITRATE, FILE_SIZE, PLAY_COUNT, FIELDS,
};
static const uint8_t audiobook_rows[] = {
    TITLE,       AUTHOR,  BOOK,      DURATION,   CHAPTERS, FILE_TYPE,
    SAMPLE_RATE, BITRATE, FILE_SIZE, PLAY_COUNT, FIELDS,
};
static const uint8_t music_video_rows[] = {
    TITLE,     ARTIST,  ALBUM,     YEAR,       GENRE,  DURATION,
    FILE_TYPE, BITRATE, FILE_SIZE, PLAY_COUNT, FIELDS,
};
static const uint8_t video_rows[] = {
    TITLE, YEAR, GENRE, DURATION, FILE_TYPE, BITRATE, FILE_SIZE, PLAY_COUNT, FIELDS,
};

/* iTunesDB media kinds, kept in the record at +0xbc (2.0.5). */
enum media_kind {
    KIND_MOVIE = 0x2,
    KIND_PODCAST = 0x4,
    KIND_AUDIOBOOK = 0x8,
    KIND_MUSIC_VIDEO = 0x20,
    KIND_TV_SHOW = 0x40,
    KIND_RENTAL = 0x8000,
    KIND_ITUNES_U = 0x200000,
};

/* MediaItem is a non-owning, generation-checked native value (24 bytes).
 * Values are produced on request and the list copies each string: text fields
 * stay in the native string until the next request, formatted numbers in value. */
static struct {
    struct osos_media_item item;
    struct osos_native_string text;
    uint8_t text_live;
    uint8_t rows[FIELDS];
    uint8_t count;
    uint8_t size;
    char value[24]; /* The longest is "4294967295 kbps". */
} info;

static void release_text(void)
{
    if (info.text_live)
        osos_native_string_destroy(&info.text);
    info.text_live = 0;
}

/* Formatted values are short: numbers, units and extensions of up to five bytes. */
static void append(const char *text)
{
    while (*text && info.size < sizeof(info.value) - 1)
        info.value[info.size++] = *text++;
    info.value[info.size] = 0;
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

static void two_digits(uint32_t number)
{
    if (number < 10)
        append("0");
    decimal(number);
}

static void number(uint32_t value, const char *unit)
{
    if (!value) {
        append("Unknown");
        return;
    }
    decimal(value);
    append(unit);
}

static const char *text(enum field field)
{
    static const uint8_t slots[FIELDS] = {
        [TITLE] = OSOS_MEDIA_TITLE,       [ARTIST] = OSOS_MEDIA_ARTIST,
        [AUTHOR] = OSOS_MEDIA_ARTIST,     [ALBUM] = OSOS_MEDIA_ALBUM,
        [PODCAST] = OSOS_MEDIA_ALBUM,     [BOOK] = OSOS_MEDIA_ALBUM,
        [GENRE] = OSOS_MEDIA_GENRE,       [COMPOSER] = OSOS_MEDIA_COMPOSER,
        [GROUPING] = OSOS_MEDIA_GROUPING,
    };
    release_text();
    osos_native_string_init(&info.text);
    info.text_live = 1;
    if (field == ALBUM_ARTIST) {
        uint16_t units[255];
        uint32_t length = cfw_album_artist_read(info.item.record, units, 255);
        osos_native_string_assign_utf16(&info.text, units, length);
    } else {
        osos_media_item_text(&info.item, (enum osos_media_text)slots[field],
                             &info.text);
    }
    const char *data = osos_native_string_data(&info.text);
    return data && *data ? data : "Unknown";
}

static void duration(uint32_t milliseconds)
{
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

static void ordinal(uint32_t value, uint16_t total)
{
    number(value, "");
    if (value && total >= value) {
        append(" of ");
        decimal(total);
    }
}

/* The extension of the native path, without the dot, in upper case. */
static void file_type(void)
{
    struct osos_native_string path;
    osos_media_item_path(&info.item, &path);
    const char *data = osos_native_string_data(&path);
    const char *dot = NULL;
    for (const char *c = data; c && *c; c++) {
        if (*c == '.')
            dot = c;
        else if (*c == '\\' || *c == '/' || *c == ':')
            dot = NULL;
    }
    unsigned length = 0;
    while (dot && dot[length + 1])
        length++;
    if (length >= 2 && length <= 5) {
        for (const char *c = dot + 1; *c; c++) {
            char upper[2] = {*c >= 'a' && *c <= 'z' ? *c - 'a' + 'A' : *c, 0};
            append(upper);
        }
    } else {
        append("Unknown");
    }
    osos_native_string_destroy(&path);
}

/* The record keeps the sample rate as an IEEE single (2.0.5). */
static void sample_rate(uint32_t bits)
{
    uint32_t exponent = bits >> 23 & 0xff, hertz = 0;
    if (!(bits >> 31) && exponent >= 127 && exponent < 150)
        hertz = ((bits & 0x7fffff) | 0x800000) >> (150 - exponent);
    if (!hertz) {
        append("Unknown");
        return;
    }
    decimal(hertz / 1000);
    if (hertz % 1000 >= 100) {
        append(".");
        decimal(hertz % 1000 / 100);
    }
    append(" kHz");
}

static const char *value(enum field field)
{
    const uint8_t *record = (const uint8_t *)info.item.record;
    struct osos_media_item *item = &info.item;
    switch (field) {
    case YEAR:
        number(osos_media_item_year(item), "");
        break;
    case DURATION:
        duration(osos_media_item_duration(item));
        break;
    case CHAPTERS:
        decimal(osos_media_item_chapter_count(item));
        break;
    /* Records keep track and disc totals beside their 16-bit ordinals. */
    case TRACK:
        ordinal(osos_media_item_track(item), *(const uint16_t *)(record + 0x86));
        break;
    case DISC:
        ordinal(osos_media_item_disc(item), *(const uint16_t *)(record + 0x8a));
        break;
    case FILE_TYPE:
        file_type();
        break;
    case SAMPLE_RATE:
        sample_rate(*(const uint32_t *)(record + 0x74));
        break;
    case BITRATE:
        number(osos_media_item_bitrate(item), " kbps");
        break;
    case FILE_SIZE: {
        uint32_t bytes = osos_media_item_file_size(item);
        if (!bytes) {
            append("Unknown");
            break;
        }
        decimal(bytes / 1048576);
        append(".");
        decimal(bytes % 1048576 * 10 / 1048576);
        append(" MiB");
        break;
    }
    case RATING: {
        int32_t rating = osos_media_item_rating(item);
        if (rating > 0 && rating <= 100) {
            decimal(rating / 20);
            append("/5");
        } else {
            append("Unrated");
        }
        break;
    }
    case PLAY_COUNT:
        decimal(osos_media_item_play_count(item));
        break;
    case SKIP_COUNT:
        decimal(osos_media_item_skip_count(item));
        break;
    default:
        return text(field);
    }
    return info.value;
}

static const uint8_t *rows_for(uint32_t kind)
{
    if (kind & KIND_AUDIOBOOK)
        return audiobook_rows;
    if (kind & KIND_PODCAST)
        return podcast_rows;
    if (kind & KIND_ITUNES_U)
        return lecture_rows;
    if (kind & KIND_MUSIC_VIDEO)
        return music_video_rows;
    if (kind & (KIND_MOVIE | KIND_TV_SHOW | KIND_RENTAL))
        return video_rows;
    return song_rows;
}

int cfw_file_info_show(void *controller, struct osos_media_item *item)
{
    if (!item || !item->record || !osos_media_item_valid(item))
        return 0;
    release_text();
    info.item = *item;
    info.count = 0;
    uint32_t kind = *(const uint32_t *)((const uint8_t *)item->record + 0xbc);
    for (const uint8_t *row = rows_for(kind); *row != FIELDS; row++)
        if (*row != CHAPTERS || osos_media_item_chapter_count(item))
            info.rows[info.count++] = *row;

    void *screen =
        osos_create_screen_controller(osos_controller_registry(), CFW_FileInfo_Screen,
                                      CFW_FileInfo_Layout, 2); /* foregroundpush */
    if (!screen)
        return 0;
    osos_controller_push(controller, screen);
    return 1;
}

int cfw_file_info_action(const char *action, uint32_t argument)
{
    (void)argument;
    return cfw_string_equal(action, "CFW_FileInfo_Select");
}

int cfw_file_info_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (property != CFW_FileInfo_Rows || type != OSOS_PROPERTY_COUNT)
        return 0;
    *out = info.count;
    return 1;
}

int cfw_file_info_indexed_property(uint32_t type, uint32_t property, uint32_t index,
                                   uintptr_t *out)
{
    if (index >= info.count || type != OSOS_PROPERTY_STRING)
        return 0;
    enum field field = (enum field)info.rows[index];
    if (property == CFW_FileInfo_Rows) {
        *out = (uintptr_t)(labels + label_offsets[field]);
        return 1;
    }
    if (property != CFW_FileInfo_Detail)
        return 0;
    info.size = 0;
    info.value[0] = 0;
    /* The list can outlive the item when the library reloads. */
    *out = (uintptr_t)(osos_media_item_valid(&info.item) ? value(field) : info.value);
    return 1;
}
