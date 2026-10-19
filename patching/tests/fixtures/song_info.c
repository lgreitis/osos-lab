#include <assert.h>
#include <stdio.h>
#include <string.h>
#define _Static_assert(...)
#include "osos.h"
#undef _Static_assert
#define CFW_PATCH_H
#define PATCH_ARM
#include "song_info_ui.h"

const struct cfw_ui_model cfw_eq_model = {0};

static int language = -1, bank, controller, screen, pushed, valid = 1;
static unsigned row_count, destroyed;
static struct {
    uint32_t duration, file_size, bitrate, year, track, disc, play_count, skip_count;
    int32_t rating;
} values = {3661000, 1572864, 320, 2020, 3, 1, 7, 0, 80};
static const char *title = "Song %u";
static const uint32_t row_ids[] = {
    CFW_INFO_TITLE, CFW_INFO_ARTIST, CFW_INFO_ALBUM, CFW_INFO_ALBUM_ARTIST,
    CFW_INFO_YEAR, CFW_INFO_GENRE, CFW_INFO_COMPOSER, CFW_INFO_GROUPING,
    CFW_INFO_DURATION, CFW_INFO_TRACK, CFW_INFO_DISC, CFW_INFO_BITRATE,
    CFW_INFO_FILE_SIZE, CFW_INFO_RATING, CFW_INFO_PLAY_COUNT, CFW_INFO_SKIP_COUNT,
};
static char rows[16][1024];
static int current_language(void) { return language; }

static void write_resource(void *target, uint32_t type, uint32_t id,
                           const void *data, uint32_t size)
{
    assert(target == &bank && type == 0x53747220 && size <= sizeof(rows[0]));
    assert(row_count < 16 && id == row_ids[row_count]);
    assert(strlen(data) + 1 == size);
    memcpy(rows[row_count++], data, size);
}

static void string_init(struct osos_native_string *string) { string->data = NULL; }
static void string_destroy(struct osos_native_string *string)
{
    (void)string;
    destroyed++;
}
static void item_text(struct osos_media_item *item, enum osos_media_text field,
                       struct osos_native_string *string)
{
    (void)item;
    string->data = (char *)(field == OSOS_MEDIA_TITLE ? title : "");
}
static void assign_utf16(struct osos_native_string *string, const uint16_t *text,
                          int32_t units)
{
    assert(units == 4 && text[0] == 'B');
    string->data = "Band";
}
uint32_t cfw_album_artist_read(struct osos_media_record *record,
                               uint16_t *text, uint32_t capacity)
{
    assert(record && capacity == 255);
    text[0] = 'B';
    return 4;
}
static void *get_registry(void) { return &bank; }
static void *create_screen(void *target, uint32_t screen_id, uint32_t layout, uint32_t mode)
{
    assert(target == &bank && screen_id == CFW_SongInfo_Screen);
    assert(layout == CFW_SongInfo_Layout && mode == 2 && row_count == 16);
    return &screen;
}
static void push(void *target, void *value)
{
    assert(target == &controller && value == &screen);
    pushed++;
}

#define osos_ui_language current_language
#define osos_resource_bank() (&bank)
#define osos_resource_set write_resource
#define osos_native_string_init string_init
#define osos_native_string_destroy string_destroy
#define osos_native_string_data(string) ((string)->data)
#define osos_native_string_assign_utf16 assign_utf16
#define osos_media_item_text item_text
#define osos_media_item_valid(item) ((void)(item), valid)
#define osos_media_item_duration(item) values.duration
#define osos_media_item_file_size(item) values.file_size
#define osos_media_item_bitrate(item) values.bitrate
#define osos_media_item_year(item) values.year
#define osos_media_item_rating(item) values.rating
#define osos_media_item_track(item) values.track
#define osos_media_item_disc(item) values.disc
#define osos_media_item_play_count(item) values.play_count
#define osos_media_item_skip_count(item) values.skip_count
#define osos_controller_registry get_registry
#define osos_create_screen_controller create_screen
#define osos_controller_push push
#undef OSOS_CONTROLLER_REGISTRY
#undef OSOS_CREATE_SCREEN_CONTROLLER
#define OSOS_CONTROLLER_REGISTRY get_registry
#define OSOS_CREATE_SCREEN_CONTROLLER create_screen
const void *cfw_resource_service_get_original(void *service, uint32_t type, uint32_t id)
{
    (void)service;
    (void)type;
    (void)id;
    return NULL;
}
void cfw_ui_register_controllers(void) {}
void cfw_ui_register_canvas(void) {}
#include "../../../payload/resources.c"
#include "../../../payload/song_info.c"

static void check_row(unsigned index, const char *expected)
{
    if (strcmp(rows[index], expected))
        fprintf(stderr, "row %u: expected '%s', got '%s'\n", index, expected, rows[index]);
    assert(!strcmp(rows[index], expected));
}

int main(void)
{
    uint16_t record[0x90 / 2] = {0};
    record[0x86 / 2] = 12;
    record[0x8a / 2] = 2;
    struct osos_media_item item = {.record = (struct osos_media_record *)record};
    cfw_song_info_show(&controller, &item);
    assert(row_count == 16 && destroyed == 7 && pushed == 1);
    const char *english[] = {
        "Title: Song %u", "Artist: Unknown", "Album: Unknown", "Album Artist: Band",
        "Year: 2020", "Genre: Unknown", "Composer: Unknown", "Grouping: Unknown",
        "Duration: 1:01:01", "Track: 3 of 12", "Disc: 1 of 2", "Bitrate: 320 kbps",
        "File size: 1.5 MiB", "Rating: 4/5", "Play count: 7", "Skip count: 0",
    };
    for (unsigned index = 0; index < 16; index++)
        check_row(index, english[index]);
    language = 4;
    row_count = 0;
    cfw_song_info_show(&controller, &item);
    check_row(0, "Titel: Song %u");
    check_row(1, "Interpret: Unbekannt");
    check_row(3, "Albuminterpret: Band");
    check_row(8, "Dauer: 1:01:01");
    check_row(9, "Titelnummer: 3 von 12");
    check_row(12, "Dateigr\xc3\xb6\xc3\x9f" "e: 1,5 MiB");
    check_row(15, "\xc3\x9c" "bersprungen: 0");
    assert(!strcmp(cfw_string(CFW_SONG_INFO_LABEL), "Titelinformationen"));
    for (uint32_t index = 0; index < cfw_translation_count; index++)
        for (unsigned row = 0; row < 16; row++)
            assert(cfw_translations[index].id != row_ids[row]);
    for (language = 0; language <= 20; language++) {
        row_count = 0;
        cfw_song_info_show(&controller, &item);
        assert(row_count == 16);
        if (language) {
            assert(cfw_translated_string(CFW_SONG_INFO_LABEL));
            assert(cfw_translated_string(CFW_SONG_TITLE_FORMAT));
            assert(cfw_translated_string(CFW_SONG_UNKNOWN));
            assert(cfw_translated_string(CFW_SONG_UNRATED));
        }
        if (language == 1)
            check_row(0, "\u30bf\u30a4\u30c8\u30eb\uff1aSong %u");
        if (language == 9)
            check_row(9, "Sorsz\xc3\xa1m: 12 k\xc3\xb6z\xc3\xbcl 3");
        if (language == 20)
            check_row(0, "\u6a19\u984c\uff1aSong %u");
    }
    language = 4;
    memset(&values, 0, sizeof(values));
    title = NULL;
    row_count = 0;
    cfw_song_info_show(&controller, &item);
    check_row(0, "Titel: Unbekannt");
    check_row(4, "Jahr: Unbekannt");
    check_row(8, "Dauer: Unbekannt");
    check_row(9, "Titelnummer: Unbekannt");
    check_row(11, "Bitrate: Unbekannt");
    check_row(12, "Dateigr\xc3\xb6\xc3\x9f" "e: Unbekannt");
    check_row(13, "Bewertung: Nicht bewertet");
    check_row(14, "Wiedergaben: 0");
    language = -1;
    values.duration = 65000;
    values.track = 13;
    values.disc = 1;
    record[0x8a / 2] = 0;
    values.rating = 101;
    row_count = 0;
    cfw_song_info_show(&controller, &item);
    check_row(8, "Duration: 1:05");
    check_row(9, "Track: 13");
    check_row(10, "Disc: 1");
    check_row(13, "Rating: Unrated");
    assert(!strcmp(cfw_string(CFW_SONG_INFO_LABEL), "Song Info"));
    row_count = 0;
    cfw_song_info_show(&controller, NULL);
    valid = 0;
    cfw_song_info_show(&controller, &item);
    valid = 1;
    item.record = NULL;
    cfw_song_info_show(&controller, &item);
    assert(!row_count && pushed == 25);
    return 0;
}