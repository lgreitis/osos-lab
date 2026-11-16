/* SPDX-License-Identifier: GPL-3.0-only */
#include "playback_log_config.h"

_Static_assert(CFW_LOG_COLUMN_COUNT <= 32, "Column mask capacity");

const char *const cfw_log_column_names[CFW_LOG_COLUMN_COUNT] = {
    [CFW_LOG_COLUMN_VERSION] = "version",
    [CFW_LOG_COLUMN_SESSION] = "session",
    [CFW_LOG_COLUMN_END_UTC] = "end_utc",
    [CFW_LOG_COLUMN_UTC_OFFSET] = "utc_offset",
    [CFW_LOG_COLUMN_START_LOCAL] = "start_local",
    [CFW_LOG_COLUMN_END_LOCAL] = "end_local",
    [CFW_LOG_COLUMN_LISTENED_MS] = "listened_ms",
    [CFW_LOG_COLUMN_LENGTH_MS] = "length_ms",
    [CFW_LOG_COLUMN_PERCENT] = "percent",
    [CFW_LOG_COLUMN_ENDED] = "ended",
    [CFW_LOG_COLUMN_APPLE] = "apple",
    [CFW_LOG_COLUMN_ARTIST] = "artist",
    [CFW_LOG_COLUMN_ALBUM_ARTIST] = "album_artist",
    [CFW_LOG_COLUMN_ALBUM] = "album",
    [CFW_LOG_COLUMN_TITLE] = "title",
    [CFW_LOG_COLUMN_TRACK] = "track",
    [CFW_LOG_COLUMN_DISC] = "disc",
    [CFW_LOG_COLUMN_YEAR] = "year",
    [CFW_LOG_COLUMN_GENRE] = "genre",
    [CFW_LOG_COLUMN_COMPOSER] = "composer",
    [CFW_LOG_COLUMN_BITRATE] = "bitrate",
    [CFW_LOG_COLUMN_SAMPLE_RATE] = "sample_rate",
    [CFW_LOG_COLUMN_RATING] = "rating",
    [CFW_LOG_COLUMN_PLAY_COUNT] = "play_count",
    [CFW_LOG_COLUMN_SKIP_COUNT] = "skip_count",
    [CFW_LOG_COLUMN_SHUFFLE] = "shuffle",
    [CFW_LOG_COLUMN_REPEAT] = "repeat",
    [CFW_LOG_COLUMN_QUEUE_POS] = "queue_pos",
    [CFW_LOG_COLUMN_QUEUE_LEN] = "queue_len",
    [CFW_LOG_COLUMN_PERSISTENT_ID] = "persistent_id",
    [CFW_LOG_COLUMN_START_UTC] = "start_utc",
    [CFW_LOG_COLUMN_POSITION_MS] = "position_ms",
};

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static char *trim(char *s)
{
    while (space(*s))
        s++;
    char *end = s;
    while (*end)
        end++;
    while (end > s && space(end[-1]))
        *--end = 0;
    return s;
}

static void columns(struct cfw_log_config *config, char *value)
{
    uint8_t selected[CFW_LOG_COLUMN_COUNT];
    unsigned count = 0;
    uint32_t seen = 0;
    for (;;) {
        char *next = value;
        while (*next && *next != ',')
            next++;
        int last = !*next;
        *next = 0;
        char *name = trim(value);
        unsigned column = 0;
        while (column < CFW_LOG_COLUMN_COUNT &&
               !equal(name, cfw_log_column_names[column]))
            column++;
        if (column == CFW_LOG_COLUMN_COUNT || count == CFW_LOG_COLUMN_COUNT ||
            (seen & (1u << column)))
            return;
        seen |= 1u << column;
        selected[count++] = column;
        if (last)
            break;
        value = next + 1;
    }
    for (unsigned i = 0; i < count; i++)
        config->columns[i] = selected[i];
    config->column_count = count;
}

static void setting(struct cfw_log_config *config, char *line)
{
    char *key = trim(line), *value = key;
    if (*key == '#')
        return;
    while (*value && *value != '=')
        value++;
    if (!*value)
        return;
    *value++ = 0;
    key = trim(key);
    value = trim(value);
    if (equal(key, "format")) {
        if (equal(value, "scrobbler"))
            config->format = CFW_LOG_SCROBBLER;
        else if (equal(value, "detailed"))
            config->format = CFW_LOG_DETAILED;
        else if (equal(value, "both"))
            config->format = CFW_LOG_BOTH;
    } else if (equal(key, "columns")) {
        columns(config, value);
    } else if (equal(value, "0") || equal(value, "1")) {
        if (equal(key, "include_podcasts"))
            config->podcasts = *value - '0';
        else if (equal(key, "include_audiobooks"))
            config->audiobooks = *value - '0';
    }
}

void cfw_log_config_parse(struct cfw_log_config *config, const char *text,
                          uint32_t size)
{
    config->format = CFW_LOG_SCROBBLER;
    config->podcasts = config->audiobooks = 0;
    config->column_count = CFW_LOG_COLUMN_COUNT;
    for (unsigned i = 0; i < CFW_LOG_COLUMN_COUNT; i++)
        config->columns[i] = i;
    if (!text || size > CFW_LOG_CONFIG_MAX)
        return;
    char line[CFW_LOG_HEADER_MAX];
    unsigned used = 0;
    int invalid = 0;
    for (uint32_t i = 0; i <= size; i++) {
        if (i == size || text[i] == '\n') {
            line[used] = 0;
            if (!invalid)
                setting(config, line);
            used = 0;
            invalid = 0;
        } else if (!text[i] || used == sizeof(line) - 1) {
            invalid = 1;
        } else if (!invalid) {
            line[used++] = text[i];
        }
    }
}

int cfw_log_config_includes(const struct cfw_log_config *config, uint32_t kind)
{
    if (kind & 0x4)
        return config->podcasts;
    if (kind & 0x8)
        return config->audiobooks;
    return kind == 0 || kind == 1;
}
