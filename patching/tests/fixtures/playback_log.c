/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define CFW_PATCH_H
#define PATCH_ARM
#define _Static_assert(condition, message)
#include "../../../payload/osos.h"
#include "../../../payload/playback_log_config.c"

static uint32_t now_us, now_utc = 1700000000;

static uint32_t mock_ticks(void)
{
    return now_us;
}

static void mock_clock(void *p)
{
    (void)p;
}

static uint32_t mock_local(void *p, int f)
{
    (void)p;
    (void)f;
    return now_utc + 7200;
}

static uint32_t mock_mac(void *p, int f)
{
    (void)p;
    (void)f;
    return now_utc + 2082844800u;
}

#undef OSOS_PLAYBACK_CLOCK
#undef OSOS_CLOCK_READ
#undef OSOS_CLOCK_SECONDS
#undef OSOS_CLOCK_MAC_SECONDS
#define osos_playback_clock mock_ticks
#define osos_clock_read mock_clock
#define osos_clock_seconds(p) mock_local(p, 1)
#define osos_clock_mac_seconds(p) mock_mac(p, 1)
static unsigned char player_data[0xb00] __attribute__((aligned(16)));
#define osos_player_get() ((struct osos_player *)player_data)
static uint32_t track_length = 180000;
#define osos_media_item_duration(i) (track_length)
#define osos_media_item_valid(i) (1)
#define osos_media_item_play_count(i) (2u)
#define osos_media_item_skip_count(i) (0u)
#define osos_media_item_track(i) (1u)
#define osos_media_item_disc(i) (1u)
#define osos_media_item_year(i) (2026u)
#define osos_media_item_bitrate(i) (256u)
#define osos_media_item_rating(i) (80u)
#define osos_playback_list_valid(p) (0)

static int flag(struct osos_player *p)
{
    (void)p;
    return 0;
}

#undef OSOS_PLAYER_IS_SHUFFLED
#undef OSOS_PLAYER_IS_REPEAT_ONE
#undef OSOS_PLAYER_IS_REPEAT_ALL
#define osos_player_is_shuffled flag
#define osos_player_is_repeat_one flag
#define osos_player_is_repeat_all flag
static const char *tag = "A\tB\nC";
#define osos_native_string_init(s) ((void)(s))
#define osos_media_item_text(i, f, s) ((void)(i), (void)(f), (void)(s))
#define osos_native_string_assign_utf16(s, t, n) ((void)(s), (void)(t), (void)(n))
#define osos_native_string_data(s) (tag)
#define osos_native_string_destroy(s) ((void)(s))

enum { FILE_COUNT = 8 };

static char output[FILE_COUNT][8192];
static char filenames[FILE_COUNT][260] = {".scrobbler.log", "playback-v3.tsv"};
static uint32_t handles[FILE_COUNT][16], sizes[FILE_COUNT];
static int short_header, read_failure;

static int file_index(const char *path)
{
    for (int i = 0; i < FILE_COUNT; i++)
        if (!strcmp(filenames[i], path))
            return i;
    return -1;
}

static int handle_index(void *h)
{
    for (int i = 0; i < FILE_COUNT; i++)
        if (h == handles[i])
            return i;
    assert(0);
    return -1;
}

static int mock_open(const struct osos_path *p, uint32_t mode, void **h)
{
    int which = file_index(p->text);
    if (mode == 1 && (which < 0 || read_failure))
        return 1;
    if (which < 0) {
        for (which = 0; which < FILE_COUNT && filenames[which][0]; which++) {
        }
        assert(which < FILE_COUNT);
        strcpy(filenames[which], p->text);
    }
    *h = handles[which];
    handles[which][8] = sizes[which];
    return 0;
}

static int mock_write(void *h, const void *data, uint32_t n, uint32_t *written)
{
    int which = handle_index(h);
    if (short_header)
        n--;
    assert(sizes[which] + n < sizeof(output[which]));
    memcpy(output[which] + sizes[which], data, n);
    sizes[which] += n;
    *written = n;
    return 0;
}

static int mock_read(void *h, void *p, uint32_t n, uint32_t *r)
{
    int which = handle_index(h);
    *r = n < sizes[which] ? n : sizes[which];
    memcpy(p, output[which], *r);
    return 0;
}

static int mock_seek(void *h, uint64_t n, int w)
{
    (void)h;
    (void)n;
    (void)w;
    return 0;
}

#define osos_file_open mock_open
#define osos_file_write mock_write
#define osos_file_read mock_read
#define osos_file_close(h) ((void)(h))
#undef OSOS_FILE_SEEK
#define osos_file_seek mock_seek
struct track_session;
static void mock_finish(struct track_session *, uint32_t);
#undef OSOS_TRACK_SESSION_FINISH
#define osos_track_session_finish mock_finish
static int notifications;
#define osos_settings_model(c) (c)
#define osos_model_notify(c, n, p) ((void)(c), (void)(n), (void)(p), notifications++)
#define osos_settings_on_off(v) ((uintptr_t)(v))
#include "../../../payload/playback_log.c"

static void mock_finish(struct track_session *s, uint32_t p)
{
    (void)p;
    if (s->state != 5)
        s->state = 6;
}

int cfw_playback_log_native_notify(struct osos_player *p, uint32_t v)
{
    (void)p;
    (void)v;
    return 1;
}

int cfw_playback_log_native_event(struct osos_player *p, void *e)
{
    (void)p;
    (void)e;
    return 1;
}

void cfw_playback_log_native_complete(struct track_session *s)
{
    if (s->state == 4)
        s->state = 5;
}

uint32_t cfw_album_artist_read(struct osos_media_record *r, uint16_t *s, uint32_t n)
{
    (void)r;
    (void)s;
    (void)n;
    return 0;
}

static int stored, legacy_present, legacy_reads;
static uint32_t stored_value, legacy_value;

int cfw_store_read_legacy(const char *path, void *out, uint32_t size)
{
    assert(!strcmp(path, "iPod_Control\\Device\\CFWLog") && size == 4);
    legacy_reads++;
    if (legacy_present == CFW_PREFERENCES_OK)
        memcpy(out, &legacy_value, size);
    return legacy_present;
}

void cfw_preferences_delete(const char *path)
{
    assert(!strcmp(path, "iPod_Control\\Device\\CFWLog"));
    legacy_present = 0;
}

uint32_t cfw_store_get(uint32_t key, void *out, uint32_t size)
{
    assert(key == CFW_STORE_PLAYBACK_LOG && size == 4);
    if (!stored)
        return 0;
    memcpy(out, &stored_value, size);
    return size;
}

void cfw_store_set(uint32_t key, const void *in, uint32_t size)
{
    assert(key == CFW_STORE_PLAYBACK_LOG && size == 4);
    memcpy(&stored_value, in, size);
    stored = 1;
}

static void configure(const char *text)
{
    cfw_log_config_parse(&config, text, (uint32_t)strlen(text));
    configure_output();
}

static unsigned count(const char *p, unsigned n, char c)
{
    unsigned v = 0;
    for (unsigned i = 0; i < n; i++)
        v += p[i] == c;
    return v;
}

int main(void)
{
    cfw_playback_log_load();
    assert(!enabled && stored && !stored_value);
    stored = 0;
    legacy_present = CFW_PREFERENCES_IO_ERROR;
    cfw_playback_log_load();
    assert(!enabled && !stored && !imported_legacy);
    legacy_present = 1;
    legacy_value = LOG_ON_RECORD;
    cfw_playback_log_load();
    assert(enabled && stored_value == 1);
    cfw_playback_log_delete_legacy();
    assert(!legacy_present && !imported_legacy);
    int reads = legacy_reads;
    stored_value = 0;
    cfw_playback_log_load();
    assert(!enabled && legacy_reads == reads);
    assert(cfw_playback_log_action(NULL, "CFW_PlaybackLog_Toggle",
                                   OSOS_ACTION_SUPPORT_QUERY));
    assert(!enabled && !notifications);
    assert(cfw_playback_log_action(NULL, "CFW_PlaybackLog_Toggle", 0));
    assert(enabled && stored_value == 1 && notifications == 1);
    uintptr_t value = 9;
    assert(
        cfw_playback_log_property(OSOS_PROPERTY_STRING, CFW_PlaybackLog_Value, &value));
    assert(value == 1);
    cfw_playback_log_reset();
    assert(!enabled && !stored_value);

    configure("");
    assert(config.format == CFW_LOG_SCROBBLER && !config.podcasts &&
           !config.audiobooks);
    configure(" # comment\r\n format = both\ninclude_podcasts=1\ninclude_audiobooks=1");
    assert(config.format == CFW_LOG_BOTH && config.podcasts && config.audiobooks);
    assert(cfw_log_config_includes(&config, 4) && cfw_log_config_includes(&config, 8));
    configure(
        "format=detailed\nformat=bad\ninclude_podcasts=2\nfoo=1\nformat=both=bad");
    assert(config.format == CFW_LOG_DETAILED && !config.podcasts);
    char huge[2048];
    memset(huge, 'x', sizeof(huge));
    cfw_log_config_parse(&config, huge, sizeof(huge));
    assert(config.format == CFW_LOG_SCROBBLER);
    memcpy(huge, "format=both", 11);
    huge[200] = '\n';
    cfw_log_config_parse(&config, huge, 201);
    assert(config.format == CFW_LOG_SCROBBLER);
    cfw_log_config_parse(&config, "format=both\0x\n", 14);
    assert(config.format == CFW_LOG_SCROBBLER);
    assert(cfw_log_config_includes(&config, 1) && cfw_log_config_includes(&config, 0));
    assert(!cfw_log_config_includes(&config, 4) &&
           !cfw_log_config_includes(&config, 8));
    assert(!cfw_log_config_includes(&config, 2) &&
           !cfw_log_config_includes(&config, 0x20));
    assert(!qualified(30000, 30000) && qualified(30001, 15001));
    assert(!qualified(180000, 89999) && qualified(180000, 90000));
    assert(!qualified(3600000, 239999) && qualified(3600000, 240000));

    assert(config.column_count == CFW_LOG_COLUMN_COUNT);
    configure("columns = title, artist, start_utc ");
    assert(config.column_count == 3 && config.columns[0] == CFW_LOG_COLUMN_TITLE &&
           config.columns[1] == CFW_LOG_COLUMN_ARTIST &&
           config.columns[2] == CFW_LOG_COLUMN_START_UTC);
    char ordered_path[260];
    strcpy(ordered_path, log_name);
    configure("columns=artist,title,start_utc");
    assert(strcmp(ordered_path, log_name));
    const char *invalid[] = {
        "columns=title,title", "columns=title,unknown", "columns=",
        "columns=title,",      "columns=,title",        "columns=title,,artist"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        configure(invalid[i]);
        assert(config.column_count == CFW_LOG_COLUMN_COUNT);
        assert(!strcmp(log_name, "playback-v3.tsv"));
    }
    configure("columns=title\ncolumns=artist,unknown");
    assert(config.column_count == 1 && config.columns[0] == CFW_LOG_COLUMN_TITLE);
    char all[CFW_LOG_CONFIG_MAX] = "columns=";
    for (unsigned i = 0; i < CFW_LOG_COLUMN_COUNT; i++) {
        if (i)
            strcat(all, ",");
        strcat(all, cfw_log_column_names[i]);
    }
    assert(strlen(all) > 96);
    configure(all);
    assert(config.column_count == CFW_LOG_COLUMN_COUNT);
    assert(!strcmp(log_name, "playback-v3.tsv"));
    assert(detailed_header_size < CFW_LOG_HEADER_MAX);
    for (unsigned i = 0; i < CFW_LOG_COLUMN_COUNT; i++)
        assert(config.columns[i] == i);
    memset(huge, ' ', sizeof(huge));
    memcpy(huge, "format=both", 11);
    huge[600] = '\n';
    cfw_log_config_parse(&config, huge, 601);
    assert(config.format == CFW_LOG_SCROBBLER);

    struct track_session current = {.state = 4};
    struct osos_player *player = osos_player_get();
    player->current = &current;
    player_data[0x93d] = 1;
    enabled = 1;
    observe(player);
    assert(listening.started && listening.start_utc == now_utc);
    now_us += 10000000;
    observe(player);
    assert(listening.listened_ms == 10000);
    player_data[0x93d] = 2;
    observe(player);
    now_us += 30000000;
    observe(player);
    assert(listening.listened_ms == 10000);
    player_data[0x93d] = 1;
    observe(player);
    now_us += 10000000;
    observe(player);
    assert(listening.listened_ms == 20000);
    player_data[0x93e] = 1;
    observe(player);
    now_us += 20000000;
    observe(player);
    assert(listening.listened_ms == 20000);
    player_data[0x93e] = 0;
    observe(player);
    now_us += 10000000;
    configure("format=both");
    finish(&current, 170000, ENDED_SKIP);
    assert(listening.listened_ms == 30000 && listening.finalized);
    assert(strstr(output[0], "\t180\tS\t1700000000\t\n"));
    assert(count(output[0], sizes[0], '\t') == 7);
    assert(strstr(output[1], "\t30000\t180000\t16\tskip\t"));
    assert(strstr(output[1], "\t1700000000\t170000\n"));
    uint32_t before = sizes[0];
    finish(&current, 170000, ENDED_SKIP);
    assert(sizes[0] == before);

    /* Repeat one reuses the native session and still starts a fresh play. */
    current.state = 4;
    now_utc += 300;
    observe(player);
    assert(listening.listened_ms == 0 && listening.start_utc == now_utc);
    now_us += 180000000;
    cfw_playback_log_complete(&current);
    assert(strstr(output[0], "\t180\tL\t1700000300\t\n"));
    before = sizes[0];
    finish(&current, track_length, ENDED_CHANGE);
    assert(sizes[0] == before);
    current.state = 4;
    observe(player);
    assert(listening.listened_ms == 0);
    listening.last_tick = UINT32_MAX - 999;
    tick(1000);
    assert(listening.listened_ms == 2);
    for (int i = 0; i < 20; i++)
        tick(listening.last_tick + 999);
    assert(listening.listened_ms == 21 && listening.remainder == 980);

    char tagbuf[4096];
    memset(tagbuf, 'x', sizeof(tagbuf) - 1);
    tagbuf[4095] = 0;
    tag = tagbuf;
    listening.listened_ms = 1000;
    log_session(&current.item, 4, 1000, ENDED_CHANGE, 0, 0);
    assert(count(line.data, line.size, '\t') == 31 && line.data[line.size - 1] == '\n');
    assert(line.size < sizeof(line.data));
    memset(tagbuf, 'x', 254);
    memcpy(tagbuf + 254, "\xe2\x82\xac", 4);
    line.size = 0;
    field_text(tagbuf);
    assert(line.size == 255 && line.data[254] == '\t');
    tag = "A\tB\nC";
    configure("format=both\ncolumns=start_utc,artist,album,title,listened_ms,length_ms,"
              "ended");
    assert(detailed_header_size ==
           strlen("start_utc\tartist\talbum\ttitle\tlistened_ms\tlength_ms\tended\n"));
    assert(!memcmp(detailed_header,
                   "start_utc\tartist\talbum\ttitle\tlistened_ms\tlength_ms\tended\n",
                   detailed_header_size));
    before = sizes[0];
    listening.start_utc = 1700000000;
    listening.listened_ms = 30000;
    log_session(&current.item, 4, 170000, ENDED_SKIP, 0, 0);
    int custom = file_index(log_name);
    assert(custom >= 2);
    const char *row = "1700000000\tA B C\tA B C\tA B C\t30000\t180000\tskip\n";
    assert(sizes[custom] == detailed_header_size + strlen(row));
    assert(!memcmp(output[custom] + detailed_header_size, row, strlen(row)));
    assert(count(output[0] + before, sizes[0] - before, '\t') == 7);
    assert(strstr(output[0] + before, "\t180\tS\t1700000000\t\n"));
    log_session(&current.item, 4, 170000, ENDED_SKIP, 0, 0);
    assert(count(output[custom], sizes[custom], '\n') == 3);
    uint32_t preserved = sizes[custom];
    configure("format=detailed\ncolumns=title,artist");
    log_session(&current.item, 4, 1, ENDED_CHANGE, 0, 0);
    int reordered = file_index(log_name);
    assert(reordered != custom && sizes[custom] == preserved);
    assert(!strcmp(output[reordered], "title\tartist\nA B C\tA B C\n"));
    configure("format=detailed\ncolumns=title");
    log_session(&current.item, 4, 1, ENDED_CHANGE, 0, 0);
    int single = file_index(log_name);
    assert(!strcmp(output[single], "title\nA B C\n"));
    /* A conflicting or unreadable header must leave the existing file intact. */
    output[single][0] = 'X';
    preserved = sizes[single];
    log_session(&current.item, 4, 1, ENDED_CHANGE, 0, 0);
    assert(sizes[single] == preserved && (failed_outputs & CFW_LOG_DETAILED));
    failed_outputs = 0;
    output[single][0] = 't';
    read_failure = 1;
    log_session(&current.item, 4, 1, ENDED_CHANGE, 0, 0);
    assert(sizes[single] == preserved && (failed_outputs & CFW_LOG_DETAILED));
    read_failure = 0;

    sizes[0] = 0;
    short_header = 1;
    scrobble(&current.item, track_length, 1000);
    assert(sizes[0] == sizeof(scrobbler_header) - 2);
    short_header = 0;
    scrobble(&current.item, track_length, 1000);
    assert(sizes[0] == sizeof(scrobbler_header) - 2);
    cfw_playback_log_reset();
    assert(!enabled && !listening.running && !listening.started);
    uint32_t elapsed = listening.listened_ms;
    now_us += 1000000;
    observe(player);
    assert(listening.listened_ms == elapsed);
    return 0;
}
