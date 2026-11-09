/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_preferences.h"
#include "cfw.h"
#include "dark_mode.h"
#include "preferences.h"
#include "store.h"

enum {
    RTP1_MAGIC = 0x31505452u,
    DARK_MODE_ON_RECORD = 0x314b5244, /* DRK1, written by Dark Mode before themes */
};

static void preference_put(unsigned char *data, uint32_t word)
{
    for (unsigned i = 0; i < 4; i++)
        data[i] = (unsigned char)(word >> (8 * i));
}

static uint32_t preference_crc(const unsigned char *data, unsigned size)
{
    return ~cfw_theme_crc32(UINT32_MAX, data, size);
}

int cfw_theme_preferences_encode(const struct cfw_theme_preferences *state,
                                 unsigned char out[CFW_THEME_PREFERENCE_BYTES])
{
    if (!state || !out || state->version != 1 || state->pending > 1 ||
        !cfw_theme_selection_valid(&state->selection))
        return 0;
    preference_put(out, RTP1_MAGIC);
    preference_put(out + 4, state->version);
    preference_put(out + 8, state->pending);
    cfw_copy(out + 12, state->selection.id, sizeof(state->selection.id));
    preference_put(out + 44, state->selection.dark);
    preference_put(out + 48, state->selection.accent);
    preference_put(out + 52, preference_crc(out, 52));
    return 1;
}

int cfw_theme_preferences_decode(const unsigned char *data, uint32_t size,
                                 struct cfw_theme_preferences *out)
{
    if (!data || !out || size != CFW_THEME_PREFERENCE_BYTES ||
        cfw_theme_u32(data) != RTP1_MAGIC || cfw_theme_u32(data + 4) != 1 ||
        cfw_theme_u32(data + 8) > 1 ||
        preference_crc(data, 52) != cfw_theme_u32(data + 52))
        return 0;
    struct cfw_theme_preferences state;
    state.version = 1;
    state.pending = cfw_theme_u32(data + 8);
    cfw_copy(state.selection.id, data + 12, sizeof(state.selection.id));
    state.selection.dark = cfw_theme_u32(data + 44);
    state.selection.accent = cfw_theme_u32(data + 48);
    if (!cfw_theme_selection_valid(&state.selection))
        return 0;
    cfw_copy(out, &state, sizeof(state));
    return 1;
}

/* Dark Mode from before themes: main's store record, or during the store
 * migration its legacy file. */
static int legacy_dark(void)
{
    uint32_t record = 0;
    if (cfw_store_get(CFW_STORE_DARK, &record, sizeof(record)) != sizeof(record) &&
        (!cfw_store_migrating() ||
         !cfw_preferences_read(CFW_DARK_MODE_LEGACY_FILE, &record, sizeof(record))))
        return 0;
    return record == DARK_MODE_ON_RECORD;
}

enum cfw_theme_preferences_status
cfw_theme_preferences_load(struct cfw_theme_preferences *out)
{
    /* Reading one extra byte detects a longer record or unsupported format. */
    unsigned char bytes[CFW_THEME_PREFERENCE_BYTES + 1];
    uint32_t size = cfw_store_get(CFW_STORE_THEME, bytes, sizeof(bytes));
    if (size)
        return cfw_theme_preferences_decode(bytes, size, out) ? CFW_THEME_PREF_VALID
                                                               : CFW_THEME_PREF_INVALID;
    if (!legacy_dark())
        return CFW_THEME_PREF_ABSENT;
    /* Dark Mode On becomes Classic dark, saved once as the theme record. */
    cfw_zero(out, sizeof(*out));
    out->version = 1;
    out->selection.dark = 1;
    cfw_theme_preferences_store(out);
    return CFW_THEME_PREF_VALID;
}

enum cfw_theme_store_result
cfw_theme_preferences_store(const struct cfw_theme_preferences *state)
{
    unsigned char bytes[CFW_THEME_PREFERENCE_BYTES], saved[CFW_THEME_PREFERENCE_BYTES];
    if (!cfw_theme_preferences_encode(state, bytes))
        return CFW_THEME_STORE_UNCHANGED;
    cfw_store_set(CFW_STORE_THEME, bytes, sizeof(bytes));
    /* The store drops a record that does not fit; read it back to confirm. */
    if (cfw_store_get(CFW_STORE_THEME, saved, sizeof(saved)) != sizeof(saved))
        return CFW_THEME_STORE_UNKNOWN;
    for (unsigned i = 0; i < sizeof(bytes); i++)
        if (bytes[i] != saved[i])
            return CFW_THEME_STORE_UNKNOWN;
    return CFW_THEME_STORE_SAVED;
}
