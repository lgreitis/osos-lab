/* SPDX-License-Identifier: GPL-3.0-only */
/* Playback log: one tab-separated row per track that stops playing, appended to
 * LOG_PATH under a header row (see docs/playback-log.md for the columns). Off by
 * default; Settings > Playback Log turns it on.
 *
 * Times come from the iPod clock; end_utc uses the iPod's time zone setting.
 * "apple" records whether the stock firmware counted the play, read from its
 * play counter around the native finish. Apple's skip counter changes elsewhere;
 * "ended" reports skips. */
#include "playback_log.h"
#include "playback_log_ui.h"
#include "osos.h"
#include "album_artist.h"
#include "cfw.h"
#include "patch.h"
#include "preferences.h"

#define LOG_PATH "playback.tsv"
#define LOG_VERSION "2"
/* Native track session states: 5 = counted at the natural end, 6 = finished. */
#define SESSION_PLAYED 5
#define SESSION_FINISHED 6
/* Song record fields: iTunes persistent ID (64-bit), sample rate (IEEE-754). */
#define RECORD_PERSISTENT_ID_OFFSET 0x110
#define RECORD_SAMPLE_RATE_OFFSET 0x74
/* Native file object: size in bytes. */
#define FILE_SIZE_OFFSET 0x20
#define MAC_EPOCH_OFFSET 2082844800u
#define LOSSLESS_KBPS 400

static const char header_row[] =
    "version\tsession\tend_utc\tutc_offset\tstart_local\tend_local\telapsed_ms\t"
    "length_ms\tpercent\tended\tapple\tartist\talbum_artist\talbum\ttitle\ttrack\t"
    "disc\tyear\tgenre\tcomposer\tbitrate\tsample_rate\tlossless\trating\t"
    "play_count\tskip_count\tshuffle\trepeat\tqueue_pos\tqueue_len\tpersistent_id\n";

struct track_session {
    uint8_t state;
    uint8_t reserved[3];
    struct osos_media_item item;
};

enum ending { ENDED_END, ENDED_SKIP, ENDED_CHANGE };

enum {
    LOG_OFF_RECORD = 0x30474f4c, /* LOG0 */
    LOG_ON_RECORD = 0x31474f4c,  /* LOG1 */
};

static const struct osos_path log_path = {0, LOG_PATH};
static const struct osos_path settings_path = {0, "iPod_Control\\Device\\CFWLog"};
static unsigned int enabled;
/* Set while MediaPlayer_AdvanceTrack (next/previous) switches tracks. */
static int advancing;
static char session[16];

static struct {
    char data[2048];
    unsigned size;
} line;

static void append(const char *text)
{
    while (*text && line.size + 1 < sizeof(line.data)) {
        char c = *text++;
        /* Keep each row on one line with tab-separated fields. */
        line.data[line.size++] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    }
}

static void raw(char c)
{
    if (line.size + 1 < sizeof(line.data))
        line.data[line.size++] = c;
}

static void decimal(uint32_t value, unsigned width)
{
    char digits[11];
    unsigned start = sizeof(digits) - 1;
    digits[start] = 0;
    do {
        digits[--start] = '0' + value % 10;
        value /= 10;
    } while (value || sizeof(digits) - 1 - start < width);
    append(digits + start);
}

/* Eight uppercase hex digits, as iTunes writes persistent IDs. */
static void hex(uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4)
        raw("0123456789ABCDEF"[(value >> shift) & 15]);
}

static void field_number(uint32_t value)
{
    decimal(value, 1);
    raw('\t');
}

static void field_text(const char *text)
{
    append(text);
    raw('\t');
}

static void field_media_text(struct osos_media_item *item, enum osos_media_text field)
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
    if (text)
        append(text);
    osos_native_string_destroy(&string);
    raw('\t');
}

/* "YYYY-MM-DD HH:MM:SS" from seconds since 1970 (Howard Hinnant's civil date). */
static void datetime(uint32_t seconds, char date_separator, char middle)
{
    uint32_t days = seconds / 86400, rest = seconds % 86400;
    uint32_t z = days + 719468, era = z / 146097, doe = z - era * 146097;
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    uint32_t day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
    decimal(yoe + era * 400 + (month <= 2), 4);
    if (date_separator)
        raw(date_separator);
    decimal(month, 2);
    if (date_separator)
        raw(date_separator);
    decimal(day, 2);
    raw(middle);
    decimal(rest / 3600, 2);
    if (date_separator)
        raw(':');
    decimal(rest / 60 % 60, 2);
    if (date_separator)
        raw(':');
    decimal(rest % 60, 2);
}

/* Local clock and UTC as seconds since 1970, from one clock reading. */
static void clock_now(uint32_t *local, uint32_t *utc)
{
    /* The native clock record is at most 12 bytes; no libc memset in the payload. */
    uint32_t clock[4];
    for (unsigned i = 0; i < 4; i++)
        clock[i] = 0;
    ((void (*)(void *))OSOS_CLOCK_READ)(clock);
    *local = ((uint32_t (*)(void *, int))OSOS_CLOCK_SECONDS)(clock, 1);
    /* Mac-epoch seconds; the flag subtracts the configured time zone and DST. */
    *utc = ((uint32_t (*)(void *, int))OSOS_CLOCK_MAC_SECONDS)(clock, 1) - MAC_EPOCH_OFFSET;
}

static void utc_offset(uint32_t local, uint32_t utc)
{
    int32_t minutes = ((int32_t)(local - utc)) / 60;
    raw(minutes < 0 ? '-' : '+');
    if (minutes < 0)
        minutes = -minutes;
    decimal((uint32_t)minutes / 60, 2);
    raw(':');
    decimal((uint32_t)minutes % 60, 2);
    raw('\t');
}

static uint32_t sample_rate_hz(struct osos_media_record *record)
{
    uint32_t bits = *(uint32_t *)((uint8_t *)record + RECORD_SAMPLE_RATE_OFFSET);
    int exponent = (int)((bits >> 23) & 0xff) - 127;
    if (bits >> 31 || exponent < 0 || exponent > 23)
        return 0;
    return ((bits & 0x7fffff) | 0x800000) >> (23 - exponent);
}

static void write_line(void)
{
    void *handle;
    uint32_t written = 0;
    if (osos_file_open(&log_path, 2, &handle) != 0)
        return;
    typedef int (*seek)(void *, uint64_t, int);
    if (((seek)OSOS_FILE_SEEK)(handle, 0, 2) == 0) {
        if (!*(uint32_t *)((uint8_t *)handle + FILE_SIZE_OFFSET))
            osos_file_write(handle, header_row, sizeof(header_row) - 1, &written);
        osos_file_write(handle, line.data, line.size, &written);
    }
    osos_file_close(handle);
}

static void log_session(struct osos_media_item *item, uint8_t state, uint32_t position,
                        enum ending ending, uint32_t plays_before, uint32_t skips_before)
{
    struct osos_player *player = osos_player_get();
    uint32_t length = osos_media_item_duration(item);
    uint32_t elapsed = state == SESSION_PLAYED ? length : position;
    if (!elapsed)
        return;
    if (length && elapsed > length)
        elapsed = length;
    uint32_t plays = osos_media_item_play_count(item);
    uint32_t skips = osos_media_item_skip_count(item);

    uint32_t local, utc;
    clock_now(&local, &utc);
    line.size = 0;
    if (!session[0]) {
        datetime(local, 0, '-');
        for (unsigned i = 0; i < line.size && i + 1 < sizeof(session); i++)
            session[i] = line.data[i];
        line.size = 0;
    }

    field_text(LOG_VERSION);
    field_text(session);
    field_number(utc);
    utc_offset(local, utc);
    datetime(local - elapsed / 1000, '-', ' ');
    raw('\t');
    datetime(local, '-', ' ');
    raw('\t');
    field_number(elapsed);
    field_number(length);
    field_number(length ? (uint32_t)((uint64_t)elapsed * 100 / length) : 0);
    field_text(ending == ENDED_END ? "end" : ending == ENDED_SKIP ? "skip" : "change");
    field_text(state == SESSION_PLAYED || plays > plays_before ? "played"
               : skips > skips_before                         ? "skipped"
                                                              : "none");
    field_media_text(item, OSOS_MEDIA_ARTIST);
    field_media_text(item, 0);
    field_media_text(item, OSOS_MEDIA_ALBUM);
    field_media_text(item, OSOS_MEDIA_TITLE);
    field_number(osos_media_item_track(item));
    field_number(osos_media_item_disc(item));
    field_number(osos_media_item_year(item));
    field_media_text(item, OSOS_MEDIA_GENRE);
    field_media_text(item, OSOS_MEDIA_COMPOSER);
    uint32_t kbps = osos_media_item_bitrate(item);
    field_number(kbps);
    field_number(item->record ? sample_rate_hz(item->record) : 0);
    field_text(kbps > LOSSLESS_KBPS ? "yes" : "no");
    /* iTunes ratings are 0-100 in steps of 20 per star. */
    field_number((uint32_t)osos_media_item_rating(item) / 20);
    field_number(plays);
    field_number(skips);
    typedef int (*player_flag)(struct osos_player *);
    field_text(((player_flag)OSOS_PLAYER_IS_SHUFFLED)(player) ? "on" : "off");
    field_text(((player_flag)OSOS_PLAYER_IS_REPEAT_ONE)(player)   ? "one"
               : ((player_flag)OSOS_PLAYER_IS_REPEAT_ALL)(player) ? "all"
                                                                  : "off");
    int queue = osos_playback_list_valid(&player->playback);
    field_number(queue ? (uint32_t)player->current_index + 1 : 0);
    field_number(queue ? osos_list_root(player->playback.list)->tracks : 0);
    if (item->record) {
        const uint32_t *id =
            (const uint32_t *)((uint8_t *)item->record + RECORD_PERSISTENT_ID_OFFSET);
        hex(id[1]);
        hex(id[0]);
    }
    raw('\n');
    write_line();
}

static void finish(struct track_session *session_object, uint32_t position, enum ending ending)
{
    typedef void (*native_finish)(struct track_session *, uint32_t);
    struct osos_player *player = osos_player_get();
    struct osos_media_item *item = &session_object->item;
    int logged = enabled && session_object && session_object == player->current &&
                 session_object->state != SESSION_FINISHED && osos_media_item_valid(item);
    uint8_t state = logged ? session_object->state : 0;
    uint32_t plays = logged ? osos_media_item_play_count(item) : 0;
    uint32_t skips = logged ? osos_media_item_skip_count(item) : 0;
    /* The native finish updates the counters; the item outlives it in the caller. */
    ((native_finish)OSOS_TRACK_SESSION_FINISH)(session_object, position);
    if (logged)
        log_session(item, state, position, state == SESSION_PLAYED ? ENDED_END : ending,
                    plays, skips);
}

/* Next/previous: MediaPlayer_AdvanceTrack can finish the current track itself. */
PATCH_ARM void cfw_playback_log_skip(struct track_session *session_object, uint32_t position)
{
    finish(session_object, position, ENDED_SKIP);
}

/* Natural end, next/previous, a new selection or stop: the current-track release. */
PATCH_ARM void cfw_playback_log_release(struct track_session *session_object,
                                        uint32_t position)
{
    finish(session_object, position, advancing ? ENDED_SKIP : ENDED_CHANGE);
}

/* MediaPlayer_AdvanceTrack selects the next track, releasing the current one. */
PATCH_ARM int cfw_playback_log_advance(struct osos_player *player, int index, int flags)
{
    typedef int (*select_track)(struct osos_player *, int, int);
    advancing = 1;
    int result = ((select_track)OSOS_PLAYER_SELECT_TRACK)(player, index, flags);
    advancing = 0;
    return result;
}

void cfw_playback_log_load(void)
{
    uint32_t record;
    enabled = cfw_preferences_read(&settings_path, &record, sizeof(record)) &&
              record == LOG_ON_RECORD;
}

int cfw_playback_log_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (type != OSOS_PROPERTY_STRING || property != CFW_PlaybackLog_Value)
        return 0;
    *out = osos_settings_on_off(enabled);
    return 1;
}

int cfw_playback_log_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_PlaybackLog_Toggle"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    uint32_t record = enabled ? LOG_OFF_RECORD : LOG_ON_RECORD;
    if (cfw_preferences_write(&settings_path, &record, sizeof(record))) {
        enabled = !enabled;
        osos_model_notify(osos_settings_model(controller), OSOS_NOTIFY_ALL,
                          CFW_PlaybackLog_Value);
    }
    return 1;
}
