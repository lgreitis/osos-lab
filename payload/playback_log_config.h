/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

enum cfw_log_format {
    CFW_LOG_SCROBBLER = 1,
    CFW_LOG_DETAILED = 2,
    CFW_LOG_BOTH = 3,
};

enum { CFW_LOG_CONFIG_MAX = 1024, CFW_LOG_HEADER_MAX = 512 };

enum cfw_log_column {
    CFW_LOG_COLUMN_VERSION,
    CFW_LOG_COLUMN_SESSION,
    CFW_LOG_COLUMN_END_UTC,
    CFW_LOG_COLUMN_UTC_OFFSET,
    CFW_LOG_COLUMN_START_LOCAL,
    CFW_LOG_COLUMN_END_LOCAL,
    CFW_LOG_COLUMN_LISTENED_MS,
    CFW_LOG_COLUMN_LENGTH_MS,
    CFW_LOG_COLUMN_PERCENT,
    CFW_LOG_COLUMN_ENDED,
    CFW_LOG_COLUMN_APPLE,
    CFW_LOG_COLUMN_ARTIST,
    CFW_LOG_COLUMN_ALBUM_ARTIST,
    CFW_LOG_COLUMN_ALBUM,
    CFW_LOG_COLUMN_TITLE,
    CFW_LOG_COLUMN_TRACK,
    CFW_LOG_COLUMN_DISC,
    CFW_LOG_COLUMN_YEAR,
    CFW_LOG_COLUMN_GENRE,
    CFW_LOG_COLUMN_COMPOSER,
    CFW_LOG_COLUMN_BITRATE,
    CFW_LOG_COLUMN_SAMPLE_RATE,
    CFW_LOG_COLUMN_RATING,
    CFW_LOG_COLUMN_PLAY_COUNT,
    CFW_LOG_COLUMN_SKIP_COUNT,
    CFW_LOG_COLUMN_SHUFFLE,
    CFW_LOG_COLUMN_REPEAT,
    CFW_LOG_COLUMN_QUEUE_POS,
    CFW_LOG_COLUMN_QUEUE_LEN,
    CFW_LOG_COLUMN_PERSISTENT_ID,
    CFW_LOG_COLUMN_START_UTC,
    CFW_LOG_COLUMN_POSITION_MS,
    CFW_LOG_COLUMN_COUNT,
};

extern const char *const cfw_log_column_names[CFW_LOG_COLUMN_COUNT];

struct cfw_log_config {
    uint8_t format, podcasts, audiobooks, column_count;
    uint8_t columns[CFW_LOG_COLUMN_COUNT];
};

void cfw_log_config_parse(struct cfw_log_config *config, const char *text,
                          uint32_t size);
int cfw_log_config_includes(const struct cfw_log_config *config, uint32_t kind);
