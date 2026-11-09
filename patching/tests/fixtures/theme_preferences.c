/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_support.h"
#include "../../../payload/theme_style.c"
#include "../../../payload/theme_input.c"
#include "../../../payload/theme_switch.c"
#include "../../../payload/theme_tint.c"

#include "../../../payload/theme_preferences.h"
#include "../../../payload/dark_mode.h"
#include "../../../payload/preferences.h"
#include "../../../payload/store.h"

static unsigned metadata_reads;
static uint32_t metadata_mask = 3;
static int metadata_error;

/* The settings store with the theme and Dark Mode records, and the legacy
 * Dark Mode file read during the store migration. */
static unsigned char theme_record[CFW_THEME_PREFERENCE_BYTES + 8];
static uint32_t theme_size, dark_record, legacy_record;
static int dark_present, legacy_present, migrating, store_full;
static unsigned store_sets, legacy_reads;

uint32_t cfw_store_get(uint32_t key, void *data, uint32_t capacity)
{
    const void *from = theme_record;
    uint32_t size = theme_size;
    if (key != CFW_STORE_THEME) {
        assert(key == CFW_STORE_DARK);
        from = &dark_record;
        size = dark_present ? sizeof(dark_record) : 0;
    }
    if (size > capacity)
        size = capacity;
    memcpy(data, from, size);
    return size;
}

void cfw_store_set(uint32_t key, const void *data, uint32_t size)
{
    assert(key == CFW_STORE_THEME && size <= sizeof(theme_record));
    store_sets++;
    if (store_full)
        return;
    memcpy(theme_record, data, size);
    theme_size = size;
}

int cfw_store_migrating(void)
{
    return migrating;
}

int cfw_preferences_read(const char *name, void *record, uint32_t size)
{
    assert(!strcmp(name, CFW_DARK_MODE_LEGACY_FILE) && size == sizeof(legacy_record));
    legacy_reads++;
    if (!legacy_present)
        return 0;
    memcpy(record, &legacy_record, size);
    return 1;
}

#include "../../../payload/theme_preferences.c"
#include "../../../payload/theme_discovery.h"

int cfw_theme_discovery_path(const char *id, const char *file, struct osos_path *out)
{
    assert(!strcmp(id, "dark") && !strcmp(file, "theme.bin"));
    out->volume = 0;
    strcpy(out->text, "themes/classic/dark.bin");
    return 1;
}

int cfw_theme_discovery_select(const char *id, uint32_t dark, int fallback,
                               struct cfw_theme_metadata *metadata,
                               struct osos_path *pack, uint32_t *selected)
{
    assert(!strcmp(id, "modern") && dark <= 1 && !fallback);
    metadata_reads++;
    if (metadata_error || !(metadata_mask & (1u << dark)))
        return 0;
    memset(metadata, 0, sizeof(*metadata));
    metadata->appearance_mask = metadata_mask;
    pack->volume = 0;
    strcpy(pack->text, dark ? "themes/modern/dark.bin" : "themes/modern/light.bin");
    *selected = dark;
    return 1;
}

#include "../../../payload/theme_boot.c"

static void codec(void)
{
    struct cfw_theme_preferences state = {1, 0, {"modern", 1, 0}}, saved;
    unsigned char bytes[CFW_THEME_PREFERENCE_BYTES + 1];
    assert(cfw_theme_preferences_encode(&state, bytes));
    static const unsigned char golden[CFW_THEME_PREFERENCE_BYTES] = {
        0x52, 0x54, 0x50, 0x31, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x6d, 0x6f, 0x64, 0x65, 0x72, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x34, 0x03, 0x3b, 0x64,
    };
    assert(!memcmp(bytes, golden, sizeof(golden)));
    assert(cfw_theme_preferences_decode(bytes, CFW_THEME_PREFERENCE_BYTES, &saved));
    assert(!memcmp(&state, &saved, sizeof(state)));
    for (unsigned i = 0; i < CFW_THEME_PREFERENCE_BYTES; i++) {
        for (unsigned bit = 0; bit < 8; bit++) {
            bytes[i] ^= 1u << bit;
            memset(&saved, 0xa5, sizeof(saved));
            assert(!cfw_theme_preferences_decode(bytes, CFW_THEME_PREFERENCE_BYTES,
                                                 &saved));
            assert(saved.version == 0xa5a5a5a5u);
            bytes[i] ^= 1u << bit;
        }
    }
    for (unsigned n = 0; n < CFW_THEME_PREFERENCE_BYTES; n++)
        assert(!cfw_theme_preferences_decode(bytes, n, &saved));
    assert(!cfw_theme_preferences_decode(bytes, sizeof(bytes), &saved));
    /* Semantic rejection even with a valid checksum. */
    const unsigned offsets[] = {4, 8, 12, 43, 44, 48};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(*offsets); i++) {
        assert(cfw_theme_preferences_encode(&state, bytes));
        bytes[offsets[i]] = 255;
        preference_put(bytes + 52, preference_crc(bytes, 52));
        assert(
            !cfw_theme_preferences_decode(bytes, CFW_THEME_PREFERENCE_BYTES, &saved));
    }
    state.pending = 2;
    assert(!cfw_theme_preferences_encode(&state, bytes));
    state.pending = 1;
    assert(cfw_theme_preferences_encode(&state, bytes));
    assert(cfw_theme_preferences_decode(bytes, CFW_THEME_PREFERENCE_BYTES, &saved));
    struct cfw_theme_selection desired;
    assert(cfw_theme_boot_selection(CFW_THEME_PREF_VALID, &saved, &desired));
    assert(!desired.id[0] && !desired.dark);
}

static void storage(void)
{
    struct cfw_theme_preferences state = {1, 0, {"modern", 1, 0}}, saved;
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_ABSENT);
    assert(!store_sets && !legacy_reads);
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_SAVED);
    assert(store_sets == 1 && theme_size == CFW_THEME_PREFERENCE_BYTES);
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_VALID);
    assert(!memcmp(&saved, &state, sizeof(state)));
    /* A record the store could not keep is not reported as saved. */
    struct cfw_theme_preferences other = {1, 0, {"", 0, 3}};
    store_full = 1;
    assert(cfw_theme_preferences_store(&other) == CFW_THEME_STORE_UNKNOWN);
    theme_size = 0;
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_UNKNOWN);
    store_full = 0;
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_SAVED);
    /* Damaged, short and longer records are invalid. */
    theme_record[20] ^= 1;
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_INVALID);
    theme_record[20] ^= 1;
    theme_size = CFW_THEME_PREFERENCE_BYTES - 4;
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_INVALID);
    theme_size = CFW_THEME_PREFERENCE_BYTES + 4;
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_INVALID);
    theme_size = CFW_THEME_PREFERENCE_BYTES;
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_VALID);
    state.version = 2;
    unsigned before = store_sets;
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_UNCHANGED);
    assert(store_sets == before);
}

static void dark_mode_migration(void)
{
    struct cfw_theme_preferences saved;
    /* Dark Mode On from before themes becomes Classic dark, saved once. */
    theme_size = 0;
    dark_present = 1;
    dark_record = 0x314b5244; /* DRK1 */
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_VALID);
    assert(saved.version == 1 && !saved.pending && !saved.selection.id[0]);
    assert(saved.selection.dark && !saved.selection.accent);
    assert(theme_size == CFW_THEME_PREFERENCE_BYTES && !legacy_reads);
    dark_record = 0x304b5244; /* DRK0 */
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_VALID);
    assert(saved.selection.dark);
    theme_size = 0;
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_ABSENT);
    /* The legacy file is read only while the store migrates. */
    dark_present = 0;
    legacy_present = 1;
    legacy_record = 0x314b5244;
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_ABSENT);
    assert(!legacy_reads);
    migrating = 1;
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_VALID);
    assert(saved.selection.dark && legacy_reads == 1);
    theme_size = 0;
    legacy_record = 0x12345678;
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_ABSENT);
    legacy_present = migrating = 0;
}

static void switch_storage_ordering(void)
{
    const struct cfw_theme_switch_ops ops = {cfw_theme_preferences_store};
    struct cfw_theme_switch switcher;
    struct cfw_theme_preferences saved;
    const struct cfw_theme_selection modern = {"modern", 1, 0}, classic = {"", 0, 0};
    const struct osos_path pack = {0, "0"}, baseline = {0, "1"};
    theme_size = 0;
    assert(cfw_theme_switch_init(&switcher, &ops));
    assert(cfw_theme_switch_store_classic(&switcher));
    assert(theme_size == CFW_THEME_PREFERENCE_BYTES);
    assert(cfw_theme_switch_apply(&switcher, &modern, &pack, &baseline));
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_VALID);
    assert(!saved.pending && !strcmp(saved.selection.id, "modern"));
    assert(!cfw_theme_switch_apply(&switcher, &classic, NULL, &baseline));
    cfw_theme_input_drained();
    /* A selection the store did not keep leaves the artwork unchanged. */
    store_full = 1;
    assert(!cfw_theme_switch_apply(&switcher, &classic, NULL, &baseline));
    assert(!strcmp(switcher.active.selection.id, "modern"));
    assert(!allocations && !live_bytes);
    store_full = 0;
    cfw_theme_input_drained();
    assert(cfw_theme_switch_apply(&switcher, &classic, NULL, &baseline));
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_VALID);
    assert(!saved.pending && !saved.selection.id[0]);
}

static void put_record(const struct cfw_theme_preferences *state)
{
    assert(cfw_theme_preferences_encode(state, theme_record));
    theme_size = CFW_THEME_PREFERENCE_BYTES;
}

static void boot_resolution(void)
{
    struct cfw_theme_boot_request request;
    struct cfw_theme_preferences state = {1, 0, {"modern", 1, 0}};
    /* An absent record is a first boot: Classic light, no recovery write. */
    theme_size = 0;
    unsigned sets = store_sets;
    assert(!cfw_theme_boot_resolve(&request));
    assert(!metadata_reads && !request.selection.id[0] && !request.selection.dark);
    assert(!request.pack.text[0] && request.appearance_mask == 3);
    assert(store_sets == sets);
    theme_size = 5;
    assert(cfw_theme_boot_resolve(&request));
    assert(!request.selection.dark && !request.pack.text[0]);
    /* A saved Classic dark selection resolves to the built-in dark pack. */
    struct cfw_theme_preferences classic_dark = {1, 0, {"", 1, 0}};
    put_record(&classic_dark);
    assert(!cfw_theme_boot_resolve(&request));
    assert(!metadata_reads && !request.selection.id[0] && request.selection.dark);
    assert(!request.pack.volume &&
           !strcmp(request.pack.text, "themes/classic/dark.bin"));
    put_record(&state);
    assert(!cfw_theme_boot_resolve(&request));
    assert(metadata_reads == 1);
    assert(request.selection.dark && !strcmp(request.selection.id, "modern"));
    assert(!strcmp(request.pack.text, "themes/modern/dark.bin"));
    metadata_mask = 1;
    assert(cfw_theme_boot_resolve(&request));
    assert(!request.selection.id[0] && !request.selection.dark);
    assert(!request.pack.text[0] && request.appearance_mask == 3);
    metadata_mask = 2;
    assert(!cfw_theme_boot_resolve(&request) && request.appearance_mask == 2);
    metadata_error = 1;
    assert(cfw_theme_boot_resolve(&request) && !request.pack.text[0]);
    metadata_error = 0;
    unsigned count = metadata_reads;
    state.pending = 1;
    put_record(&state);
    assert(cfw_theme_boot_resolve(&request));
    assert(metadata_reads == count);
    theme_record[0] ^= 1;
    assert(cfw_theme_boot_resolve(&request));
    assert(metadata_reads == count);
    assert(!request.pack.text[0] && !request.selection.id[0]);
}

int main(void)
{
    memcpy(original, pixels, sizeof(original));
    make_target(0);
    make_baseline();
    fix_crc();
    codec();
    storage();
    dark_mode_migration();
    switch_storage_ordering();
    boot_resolution();
    return 0;
}
