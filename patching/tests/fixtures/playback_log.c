/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define CFW_PATCH_H
#define PATCH_ARM
#define _Static_assert(condition, message)
#include "../../../payload/osos.h"

static uint32_t now_utc = 1700000000;

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

#undef OSOS_CLOCK_READ
#undef OSOS_CLOCK_SECONDS
#undef OSOS_CLOCK_MAC_SECONDS
#define OSOS_CLOCK_READ ((uintptr_t)mock_clock)
#define OSOS_CLOCK_SECONDS ((uintptr_t)mock_local)
#define OSOS_CLOCK_MAC_SECONDS ((uintptr_t)mock_mac)
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
#define OSOS_PLAYER_IS_SHUFFLED ((uintptr_t)flag)
#define OSOS_PLAYER_IS_REPEAT_ONE ((uintptr_t)flag)
#define OSOS_PLAYER_IS_REPEAT_ALL ((uintptr_t)flag)
static const char *tag = "A\tB\nC";
#define osos_native_string_init(s) ((void)(s))
#define osos_media_item_text(i, f, s) ((void)(i), (void)(f), (void)(s))
#define osos_native_string_assign_utf16(s, t, n) ((void)(s), (void)(t), (void)(n))
#define osos_native_string_data(s) (tag)
#define osos_native_string_destroy(s) ((void)(s))
static char output[8192];
static uint32_t handle[16], output_size;
static int short_write;

static int mock_open(const struct osos_path *p, uint32_t mode, void **h)
{
    assert(!strcmp(p->text, "playback.tsv") && mode == 2);
    *h = handle;
    handle[8] = output_size;
    return 0;
}

static int mock_write(void *h, const void *data, uint32_t n, uint32_t *written)
{
    assert(h == handle);
    if (short_write)
        n--;
    assert(output_size + n < sizeof(output));
    memcpy(output + output_size, data, n);
    output_size += n;
    *written = n;
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
#define osos_file_close(h) ((void)(h))
#undef OSOS_FILE_SEEK
#define OSOS_FILE_SEEK ((uintptr_t)mock_seek)
struct track_session;
static void mock_finish(struct track_session *, uint32_t);
#undef OSOS_TRACK_SESSION_FINISH
#define OSOS_TRACK_SESSION_FINISH ((uintptr_t)mock_finish)
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

int cfw_preferences_read(const char *path, void *out, uint32_t size)
{
    assert(!strcmp(path, "iPod_Control\\Device\\CFWLog") && size == 4);
    legacy_reads++;
    if (legacy_present)
        memcpy(out, &legacy_value, size);
    return legacy_present;
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

static unsigned count(const char *p, unsigned size, char c)
{
    unsigned found = 0;
    for (unsigned i = 0; i < size; i++)
        found += p[i] == c;
    return found;
}

int main(void)
{
    cfw_playback_log_load();
    assert(!enabled && stored && !stored_value);
    stored = 0;
    legacy_present = 1;
    legacy_value = LOG_ON_RECORD;
    cfw_playback_log_load();
    assert(enabled && stored_value == 1);
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

    line.size = 0;
    field_text("a\tb\nc");
    assert(line.size == 6 && !memcmp(line.data, "a b c\t", 6));
    char long_tag[4096];
    memset(long_tag, 'x', sizeof(long_tag) - 1);
    long_tag[4095] = 0;
    line.size = 0;
    for (int i = 0; i < 6; i++)
        field_text(long_tag);
    for (int i = 0; i < 24; i++)
        field_number(UINT32_MAX);
    hex(UINT32_MAX);
    hex(UINT32_MAX);
    raw('\n');
    assert(count(line.data, line.size, '\t') == 30 && line.data[line.size - 1] == '\n');
    assert(line.size < sizeof(line.data));
    memset(long_tag, 'x', 254);
    memcpy(long_tag + 254, "\xe2\x82\xac", 4);
    line.size = 0;
    field_text(long_tag);
    assert(line.size == 255 && line.data[254] == '\t');

    struct track_session current = {.state = 4}, prepared = {.state = 4};
    osos_player_get()->current = &current;
    enabled = 1;
    cfw_playback_log_complete(&prepared);
    assert(output_size == 0);
    cfw_playback_log_complete(&current);
    assert(current.state == 5 && count(output, output_size, '\n') == 2);
    uint32_t before = output_size;
    finish(&current, track_length, ENDED_CHANGE);
    assert(output_size == before);
    current.state = 4;
    cfw_playback_log_complete(&current);
    assert(count(output, output_size, '\n') == 3);
    current.state = 4;
    finish(&current, 10000, ENDED_SKIP);
    assert(current.state == 6 && count(output, output_size, '\n') == 4);
    assert(count(line.data, line.size, '\t') == 30);
    before = output_size;
    finish(&current, 10000, ENDED_SKIP);
    assert(output_size == before);
    enabled = 0;
    current.state = 4;
    cfw_playback_log_complete(&current);
    assert(output_size == before);

    output_size = 0;
    short_write = 1;
    write_line();
    assert(write_failed && output_size == sizeof(header_row) - 2);
    before = output_size;
    short_write = 0;
    write_line();
    assert(output_size == before);
    write_failed = 0;
    short_write = 1;
    write_line();
    assert(write_failed && output_size == before + line.size - 1);
    puts("Playback log: migration, reset, UTF-8 rows, repeated completions and short "
         "writes passed");
    return 0;
}
