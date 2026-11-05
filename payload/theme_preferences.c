/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_preferences.h"
#include "osos.h"

static const struct osos_path theme_preferences_path = {
    0, "iPod_Control\\Device\\CFWTheme"};

static uint32_t preference_word(const unsigned char *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void preference_put(unsigned char *data, uint32_t word)
{
    for (unsigned i = 0; i < 4; i++)
        data[i] = (unsigned char)(word >> (8 * i));
}

static uint32_t preference_crc(const unsigned char *data, unsigned size)
{
    uint32_t crc = UINT32_MAX;
    while (size--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

int cfw_theme_preferences_encode(const struct cfw_theme_preferences *state,
                                 unsigned char out[CFW_THEME_PREFERENCE_BYTES])
{
    if (!state || !out || state->version != 1 || state->pending > 1 ||
        !cfw_theme_selection_valid(&state->selection))
        return 0;
    preference_put(out, 0x31505452u); /* RTP1 */
    preference_put(out + 4, state->version);
    preference_put(out + 8, state->pending);
    for (unsigned i = 0; i < sizeof(state->selection.id); i++)
        out[12 + i] = state->selection.id[i];
    preference_put(out + 44, state->selection.dark);
    preference_put(out + 48, state->selection.accent);
    preference_put(out + 52, preference_crc(out, 52));
    return 1;
}

int cfw_theme_preferences_decode(const unsigned char *data, uint32_t size,
                                 struct cfw_theme_preferences *out)
{
    if (!data || !out || size != CFW_THEME_PREFERENCE_BYTES ||
        preference_word(data) != 0x31505452u || preference_word(data + 4) != 1 ||
        preference_word(data + 8) > 1 ||
        preference_crc(data, 52) != preference_word(data + 52))
        return 0;
    struct cfw_theme_preferences state;
    state.version = 1;
    state.pending = preference_word(data + 8);
    for (unsigned i = 0; i < sizeof(state.selection.id); i++)
        state.selection.id[i] = data[12 + i];
    state.selection.dark = preference_word(data + 44);
    state.selection.accent = preference_word(data + 48);
    if (!cfw_theme_selection_valid(&state.selection))
        return 0;
    /* Byte copy avoids freestanding compiler-generated memcpy. */
    unsigned char *p = (unsigned char *)out;
    const unsigned char *q = (const unsigned char *)&state;
    for (unsigned i = 0; i < sizeof(state); i++)
        p[i] = q[i];
    return 1;
}

enum cfw_theme_preferences_status
cfw_theme_preferences_load(struct cfw_theme_preferences *out)
{
    void *handle;
    int error = osos_file_open(&theme_preferences_path, 1, &handle);
    if (error)
        return error == -43 ? CFW_THEME_PREF_ABSENT : CFW_THEME_PREF_INVALID;
    unsigned char bytes[CFW_THEME_PREFERENCE_BYTES + 1];
    uint32_t count = 0;
    error = osos_file_read(handle, bytes, sizeof(bytes), &count);
    osos_file_close(handle);
    /* Reading one extra byte detects a trailing record or unsupported format. */
    return !error && cfw_theme_preferences_decode(bytes, count, out)
               ? CFW_THEME_PREF_VALID
               : CFW_THEME_PREF_INVALID;
}

enum cfw_theme_store_result
cfw_theme_preferences_store(const struct cfw_theme_preferences *state)
{
    unsigned char bytes[CFW_THEME_PREFERENCE_BYTES];
    if (!cfw_theme_preferences_encode(state, bytes))
        return CFW_THEME_STORE_UNCHANGED;
    void *handle;
    if (osos_file_open(&theme_preferences_path, 2, &handle))
        return CFW_THEME_STORE_UNKNOWN;
    uint32_t count = 0;
    int error = osos_file_write(handle, bytes, sizeof(bytes), &count);
    /* Flush before resizing: set-size does not drain the buffered writer. */
    int sync_error = osos_file_sync(handle);
    int size_error = 0;
    if (!error && count == sizeof(bytes) && !sync_error) {
        size_error = osos_file_set_size(handle, sizeof(bytes));
        sync_error = osos_file_sync(handle);
    }
    osos_file_close(handle);
    if (error || count != sizeof(bytes) || sync_error || size_error)
        return CFW_THEME_STORE_UNKNOWN;
    /* Verify the saved contents and exact file length after closing. */
    struct cfw_theme_preferences saved;
    unsigned char actual[CFW_THEME_PREFERENCE_BYTES];
    if (cfw_theme_preferences_load(&saved) != CFW_THEME_PREF_VALID ||
        !cfw_theme_preferences_encode(&saved, actual))
        return CFW_THEME_STORE_UNKNOWN;
    for (unsigned i = 0; i < sizeof(bytes); i++)
        if (bytes[i] != actual[i])
            return CFW_THEME_STORE_UNKNOWN;
    return CFW_THEME_STORE_DURABLE;
}
