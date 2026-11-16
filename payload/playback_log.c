/* SPDX-License-Identifier: GPL-3.0-only */
#include "playback_log.h"
#include "playback_log_ui.h"
#include "osos.h"
#include "album_artist.h"
#include "cfw.h"
#include "patch.h"
#include "preferences.h"
#include "store.h"
#include "playback_log_config.h"

#define LOG_PATH "playback-v3.tsv"
#define LOG_VERSION "3"
/* Native track session states: 5 = counted at the natural end, 6 = finished. */
#define SESSION_PLAYED 5
#define SESSION_FINISHED 6
/* Song record fields: iTunes persistent ID (64-bit), sample rate (IEEE-754). */
#define RECORD_PERSISTENT_ID_OFFSET 0x110
#define RECORD_SAMPLE_RATE_OFFSET 0x74
/* Native file object: size in bytes. */
#define FILE_SIZE_OFFSET 0x20
#define MAC_EPOCH_OFFSET 2082844800u

static char detailed_header[CFW_LOG_HEADER_MAX];
static uint32_t detailed_header_size;

struct track_session {
    uint8_t state;
    uint8_t reserved[3];
    struct osos_media_item item;
};

enum ending { ENDED_END, ENDED_SKIP, ENDED_CHANGE };

enum {
    LOG_ON_RECORD = 0x31474f4c, /* LOG1 */
};

static struct osos_path log_path = {0, LOG_PATH};
static const char settings_path[] = "iPod_Control\\Device\\CFWLog";
static const struct osos_path scrobbler_path = {0, ".scrobbler.log"};
static const struct osos_path config_path = {0,
                                             "iPod_Control\\Device\\playback-log.cfg"};
static const char scrobbler_header[] =
    "#AUDIOSCROBBLER/1.1\n#TZ/UTC\n#CLIENT/RepriseOS\n";
static struct cfw_log_config config;

static struct {
    struct track_session *object;
    struct osos_media_record *record;
    uint32_t last_tick, listened_ms, remainder, start_local, start_utc;
    unsigned running, started, finalized;
} listening;

extern int cfw_playback_log_native_notify(struct osos_player *, uint32_t);
extern int cfw_playback_log_native_event(struct osos_player *, void *);
extern void cfw_playback_log_native_complete(struct track_session *);

static unsigned int enabled;
/* Set while MediaPlayer_AdvanceTrack (next/previous) switches tracks. */
static int advancing;
static char session[16];
static unsigned failed_outputs;

static struct {
    char data[2048];
    unsigned size;
} line;

static void append(const char *text)
{
    /* Six tag fields must leave room for the remaining columns and newline. */
    unsigned count = 0;
    while (count < 255 && text[count])
        count++;
    if (count == 255)
        while (count && ((unsigned char)text[count] & 0xc0) == 0x80)
            count--;
    for (unsigned i = 0; i < count && line.size + 1 < sizeof(line.data); i++) {
        char c = text[i];
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
    *utc = ((uint32_t (*)(void *, int))OSOS_CLOCK_MAC_SECONDS)(clock, 1) -
           MAC_EPOCH_OFFSET;
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

static void configure_output(void)
{
    line.size = 0;
    uint32_t hash = 2166136261u;
    int custom = config.column_count != CFW_LOG_COLUMN_COUNT;
    for (unsigned i = 0; i < config.column_count; i++) {
        unsigned column = config.columns[i];
        custom |= column != i;
        field_text(cfw_log_column_names[column]);
    }
    line.data[line.size - 1] = '\n';
    detailed_header_size = line.size;
    for (unsigned i = 0; i < line.size; i++) {
        detailed_header[i] = line.data[i];
        hash = (hash ^ (uint8_t)line.data[i]) * 16777619u;
    }
    line.size = 0;
    append("playback-v3");
    if (custom) {
        raw('-');
        hex(hash);
    }
    append(".tsv");
    for (unsigned i = 0; i < line.size; i++)
        log_path.text[i] = line.data[i];
    log_path.text[line.size] = 0;
    line.size = 0;
}

/* A layout-specific filename keeps old reports; checking the header also
 * protects against collisions or a file replaced while connected over USB. */
static int detailed_header_matches(void)
{
    void *handle;
    if (osos_file_open(&log_path, 1, &handle) != 0)
        return 0;
    if (!*(uint32_t *)((uint8_t *)handle + FILE_SIZE_OFFSET)) {
        osos_file_close(handle);
        return 0;
    }
    char data[CFW_LOG_HEADER_MAX];
    uint32_t bytes = 0;
    int valid = osos_file_read(handle, data, detailed_header_size, &bytes) == 0 &&
                bytes == detailed_header_size;
    osos_file_close(handle);
    for (unsigned i = 0; valid && i < bytes; i++)
        valid = data[i] == detailed_header[i];
    return valid ? 1 : -1;
}

static void write_line(const struct osos_path *path, const char *header, uint32_t bytes)
{
    unsigned output = path == &scrobbler_path ? CFW_LOG_SCROBBLER : CFW_LOG_DETAILED;
    if (failed_outputs & output)
        return;
    int header_matches = path == &log_path ? detailed_header_matches() : 1;
    if (header_matches < 0) {
        failed_outputs |= output;
        return;
    }
    void *handle;
    uint32_t written = 0;
    if (osos_file_open(path, 2, &handle) != 0)
        return;
    if (!header_matches && *(uint32_t *)((uint8_t *)handle + FILE_SIZE_OFFSET)) {
        failed_outputs |= output;
        osos_file_close(handle);
        return;
    }
    typedef int (*seek)(void *, uint64_t, int);
    if (((seek)OSOS_FILE_SEEK)(handle, 0, 2) == 0) {
        if (!*(uint32_t *)((uint8_t *)handle + FILE_SIZE_OFFSET) &&
            (osos_file_write(handle, header, bytes, &written) != 0 ||
             written != bytes)) {
            failed_outputs |= output;
            osos_file_close(handle);
            return;
        }
        /* A partial row cannot be retried without duplicating its prefix. */
        if (osos_file_write(handle, line.data, line.size, &written) != 0 ||
            written != line.size)
            failed_outputs |= output;
    }
    osos_file_close(handle);
}

/* Read on existing player events and transitions; subtraction tolerates one
 * wrap of the native 32-bit microsecond clock (about 71 minutes). */
static void tick(uint32_t now)
{
    if (listening.running) {
        uint32_t delta = now - listening.last_tick;
        uint32_t ms = delta / 1000;
        listening.remainder += delta % 1000;
        ms += listening.remainder / 1000;
        listening.remainder %= 1000;
        listening.listened_ms = UINT32_MAX - listening.listened_ms < ms
                                    ? UINT32_MAX
                                    : listening.listened_ms + ms;
    }
    listening.last_tick = now;
}

static uint32_t ticks(void)
{
    return ((uint32_t (*)(void))OSOS_PLAYBACK_CLOCK)();
}

static void observe(struct osos_player *player)
{
    if (!enabled)
        return;
    tick(ticks());
    struct track_session *current = player->current;
    if (current != listening.object ||
        (current && current->item.record != listening.record) ||
        (current && listening.finalized &&
         (current->state == 3 || current->state == 4))) {
        listening.object = current;
        listening.record = current ? current->item.record : NULL;
        listening.listened_ms = listening.remainder = 0;
        listening.started = listening.finalized = 0;
    }
    const uint8_t *state = (const uint8_t *)player;
    listening.running = current && !listening.finalized && state[0x93d] == 1 &&
                        !state[0x93e] && (current->state == 3 || current->state == 4);
    if (listening.running && !listening.started) {
        clock_now(&listening.start_local, &listening.start_utc);
        listening.started = 1;
    }
}

static int qualified(uint32_t length, uint32_t listened)
{
    uint32_t threshold = length / 2 + length % 2;
    if (threshold > 240000)
        threshold = 240000;
    return length > 30000 && listened >= threshold;
}

static void scrobble(struct osos_media_item *item, uint32_t length, uint32_t elapsed)
{
    line.size = 0;
    field_media_text(item, OSOS_MEDIA_ARTIST);
    field_media_text(item, OSOS_MEDIA_ALBUM);
    field_media_text(item, OSOS_MEDIA_TITLE);
    field_number(osos_media_item_track(item));
    field_number(length / 1000);
    field_text(qualified(length, elapsed) ? "L" : "S");
    field_number(listening.start_utc);
    raw('\n');
    write_line(&scrobbler_path, scrobbler_header, sizeof(scrobbler_header) - 1);
}

struct log_record {
    struct osos_media_item *item;
    uint32_t length, position, local, utc, plays, skips, plays_before, skips_before;
    enum ending ending;
    uint8_t state;
};

static void detailed_column(unsigned column, const struct log_record *record)
{
    struct osos_media_item *item = record->item;
    struct osos_player *player = osos_player_get();
    typedef int (*player_flag)(struct osos_player *);
    switch (column) {
    case CFW_LOG_COLUMN_VERSION:
        field_text(LOG_VERSION);
        break;
    case CFW_LOG_COLUMN_SESSION:
        field_text(session);
        break;
    case CFW_LOG_COLUMN_END_UTC:
        field_number(record->utc);
        break;
    case CFW_LOG_COLUMN_UTC_OFFSET:
        utc_offset(record->local, record->utc);
        break;
    case CFW_LOG_COLUMN_START_LOCAL:
        datetime(listening.start_local, '-', ' ');
        raw('\t');
        break;
    case CFW_LOG_COLUMN_END_LOCAL:
        datetime(record->local, '-', ' ');
        raw('\t');
        break;
    case CFW_LOG_COLUMN_LISTENED_MS:
        field_number(listening.listened_ms);
        break;
    case CFW_LOG_COLUMN_LENGTH_MS:
        field_number(record->length);
        break;
    case CFW_LOG_COLUMN_PERCENT:
        field_number(record->length ? (uint32_t)((uint64_t)listening.listened_ms * 100 /
                                                 record->length)
                                    : 0);
        break;
    case CFW_LOG_COLUMN_ENDED:
        field_text(record->ending == ENDED_END    ? "end"
                   : record->ending == ENDED_SKIP ? "skip"
                                                  : "change");
        break;
    case CFW_LOG_COLUMN_APPLE:
        field_text(record->state == SESSION_PLAYED ||
                           record->plays > record->plays_before
                       ? "played"
                   : record->skips > record->skips_before ? "skipped"
                                                          : "none");
        break;
    case CFW_LOG_COLUMN_ARTIST:
        field_media_text(item, OSOS_MEDIA_ARTIST);
        break;
    case CFW_LOG_COLUMN_ALBUM_ARTIST:
        field_media_text(item, 0);
        break;
    case CFW_LOG_COLUMN_ALBUM:
        field_media_text(item, OSOS_MEDIA_ALBUM);
        break;
    case CFW_LOG_COLUMN_TITLE:
        field_media_text(item, OSOS_MEDIA_TITLE);
        break;
    case CFW_LOG_COLUMN_TRACK:
        field_number(osos_media_item_track(item));
        break;
    case CFW_LOG_COLUMN_DISC:
        field_number(osos_media_item_disc(item));
        break;
    case CFW_LOG_COLUMN_YEAR:
        field_number(osos_media_item_year(item));
        break;
    case CFW_LOG_COLUMN_GENRE:
        field_media_text(item, OSOS_MEDIA_GENRE);
        break;
    case CFW_LOG_COLUMN_COMPOSER:
        field_media_text(item, OSOS_MEDIA_COMPOSER);
        break;
    case CFW_LOG_COLUMN_BITRATE:
        field_number(osos_media_item_bitrate(item));
        break;
    case CFW_LOG_COLUMN_SAMPLE_RATE:
        field_number(item->record ? sample_rate_hz(item->record) : 0);
        break;
    case CFW_LOG_COLUMN_RATING:
        field_number((uint32_t)osos_media_item_rating(item) / 20);
        break;
    case CFW_LOG_COLUMN_PLAY_COUNT:
        field_number(record->plays);
        break;
    case CFW_LOG_COLUMN_SKIP_COUNT:
        field_number(record->skips);
        break;
    case CFW_LOG_COLUMN_SHUFFLE:
        field_text(((player_flag)OSOS_PLAYER_IS_SHUFFLED)(player) ? "on" : "off");
        break;
    case CFW_LOG_COLUMN_REPEAT:
        field_text(((player_flag)OSOS_PLAYER_IS_REPEAT_ONE)(player)   ? "one"
                   : ((player_flag)OSOS_PLAYER_IS_REPEAT_ALL)(player) ? "all"
                                                                      : "off");
        break;
    case CFW_LOG_COLUMN_QUEUE_POS:
        field_number(osos_playback_list_valid(&player->playback)
                         ? (uint32_t)player->current_index + 1
                         : 0);
        break;
    case CFW_LOG_COLUMN_QUEUE_LEN:
        field_number(osos_playback_list_valid(&player->playback)
                         ? osos_list_root(player->playback.list)->tracks
                         : 0);
        break;
    case CFW_LOG_COLUMN_PERSISTENT_ID:
        if (item->record) {
            const uint32_t *id = (const uint32_t *)((uint8_t *)item->record +
                                                    RECORD_PERSISTENT_ID_OFFSET);
            hex(id[1]);
            hex(id[0]);
        }
        raw('\t');
        break;
    case CFW_LOG_COLUMN_START_UTC:
        field_number(listening.start_utc);
        break;
    case CFW_LOG_COLUMN_POSITION_MS:
        field_number(record->position);
        break;
    }
}

static void log_session(struct osos_media_item *item, uint8_t state, uint32_t position,
                        enum ending ending, uint32_t plays_before,
                        uint32_t skips_before)
{
    uint32_t length = osos_media_item_duration(item);
    uint32_t elapsed = listening.listened_ms;
    if (!elapsed)
        return;
    uint32_t kind = item->record ? *(uint32_t *)((uint8_t *)item->record + 0xbc) : 0;
    if (!cfw_log_config_includes(&config, kind))
        return;
    if (config.format & CFW_LOG_SCROBBLER)
        scrobble(item, length, elapsed);
    if (!(config.format & CFW_LOG_DETAILED))
        return;
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

    const struct log_record record = {
        .item = item,
        .length = length,
        .position = position,
        .local = local,
        .utc = utc,
        .plays = plays,
        .skips = skips,
        .plays_before = plays_before,
        .skips_before = skips_before,
        .ending = ending,
        .state = state,
    };
    for (unsigned i = 0; i < config.column_count; i++)
        detailed_column(config.columns[i], &record);
    line.data[line.size - 1] = '\n';
    write_line(&log_path, detailed_header, detailed_header_size);
}

static void finish(struct track_session *session_object, uint32_t position,
                   enum ending ending)
{
    typedef void (*native_finish)(struct track_session *, uint32_t);
    struct osos_player *player = osos_player_get();
    struct osos_media_item *item = session_object ? &session_object->item : NULL;
    if (enabled)
        tick(ticks());
    int logged = enabled && session_object && session_object == player->current &&
                 session_object == listening.object && listening.started &&
                 !listening.finalized && session_object->state != SESSION_FINISHED &&
                 osos_media_item_valid(item);
    if (logged) {
        listening.running = 0;
        listening.finalized = 1;
    }
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
PATCH_ARM void cfw_playback_log_skip(struct track_session *session_object,
                                     uint32_t position)
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

PATCH_ARM int cfw_playback_log_notify(struct osos_player *player, uint32_t property)
{
    observe(player);
    return cfw_playback_log_native_notify(player, property);
}

PATCH_ARM int cfw_playback_log_event(struct osos_player *player, void *event)
{
    observe(player);
    int result = cfw_playback_log_native_event(player, event);
    observe(player);
    return result;
}

PATCH_ARM void cfw_playback_log_complete(struct track_session *object)
{
    if (enabled)
        tick(ticks());
    int logged = enabled && object && object == osos_player_get()->current &&
                 object == listening.object && listening.started &&
                 !listening.finalized && object->state == 4 &&
                 osos_media_item_valid(&object->item);
    cfw_playback_log_native_complete(object);
    if (logged && object->state == SESSION_PLAYED) {
        listening.running = 0;
        listening.finalized = 1;
        log_session(&object->item, SESSION_PLAYED,
                    osos_media_item_duration(&object->item), ENDED_END, 0, 0);
    }
}

void cfw_playback_log_load(void)
{
    uint32_t record = 0;
    /* Import the PR's legacy switch even when other settings already migrated. */
    if (!cfw_store_get(CFW_STORE_PLAYBACK_LOG, &record, sizeof(record))) {
        uint32_t legacy;
        record = cfw_preferences_read(settings_path, &legacy, sizeof(legacy)) &&
                 legacy == LOG_ON_RECORD;
        cfw_store_set(CFW_STORE_PLAYBACK_LOG, &record, sizeof(record));
    }
    enabled = record == 1;
    cfw_log_config_parse(&config, NULL, 0);
    void *handle;
    if (osos_file_open(&config_path, 1, &handle) == 0) {
        char data[CFW_LOG_CONFIG_MAX + 1];
        uint32_t bytes = 0;
        if (osos_file_read(handle, data, sizeof(data), &bytes) == 0)
            cfw_log_config_parse(&config, data, bytes);
        osos_file_close(handle);
    }
    configure_output();
}

void cfw_playback_log_reset(void)
{
    enabled = 0;
    listening.running = listening.started = 0;
    uint32_t record = 0;
    cfw_store_set(CFW_STORE_PLAYBACK_LOG, &record, sizeof(record));
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
    enabled = !enabled;
    listening.object = NULL;
    listening.running = listening.started = listening.finalized = 0;
    uint32_t record = enabled;
    cfw_store_set(CFW_STORE_PLAYBACK_LOG, &record, sizeof(record));
    observe(osos_player_get());
    osos_model_notify(osos_settings_model(controller), OSOS_NOTIFY_ALL,
                      CFW_PlaybackLog_Value);
    return 1;
}
