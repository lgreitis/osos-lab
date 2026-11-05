/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_support.h"
#include "../../../payload/theme_style.c"
#include "../../../payload/theme_input.c"
#include "../../../payload/theme_switch.c"
#include "../../../payload/theme_preferences.h"

static unsigned char disk[CFW_THEME_PREFERENCE_BYTES + 8];
static uint32_t disk_size, cursor;
static int open_error, read_error, write_error, sync_error, size_error;
static uint32_t write_limit = CFW_THEME_PREFERENCE_BYTES;
static unsigned storage_opens, storage_closes, syncs, fail_sync_at;
static int corrupt_readback;
static uint32_t legacy_record;
static unsigned legacy_reads, metadata_reads;
static uint32_t metadata_mask = 3;
static int legacy_error, metadata_error;
static int reading_legacy;

static int preference_open(const struct osos_path *path, uint32_t mode, void **out)
{
    reading_legacy = !strcmp(path->text, "iPod_Control\\Device\\CFWDark");
    if (reading_legacy) {
        assert(mode == 1);
        legacy_reads++;
        if (legacy_error)
            return legacy_error;
        storage_opens++;
        *out = disk;
        return 0;
    }
    assert(!strcmp(path->text, "iPod_Control\\Device\\CFWTheme"));
    assert(mode == 1 || mode == 2);
    if (open_error)
        return open_error;
    cursor = 0;
    storage_opens++;
    *out = disk;
    return 0;
}

static int preference_read(void *handle, void *out, uint32_t size, uint32_t *count)
{
    assert(handle == disk);
    if (reading_legacy) {
        assert(size == sizeof(legacy_record));
        memcpy(out, &legacy_record, size);
        *count = size;
        return 0;
    }
    if (read_error)
        return read_error;
    if (size > disk_size - cursor)
        size = disk_size - cursor;
    memcpy(out, disk + cursor, size);
    if (corrupt_readback && size)
        ((unsigned char *)out)[0] ^= 1;
    cursor += size;
    *count = size;
    return 0;
}

static int preference_write(void *handle, const void *data, uint32_t size,
                            uint32_t *count)
{
    assert(handle == disk);
    if (size > write_limit)
        size = write_limit;
    assert(cursor + size <= sizeof(disk));
    memcpy(disk + cursor, data, size);
    cursor += size;
    if (disk_size < cursor)
        disk_size = cursor;
    *count = size;
    return write_error;
}

static int preference_set_size(void *handle, uint32_t bytes)
{
    assert(handle == disk && bytes <= sizeof(disk));
    if (size_error)
        return size_error;
    disk_size = bytes;
    return 0;
}

static int preference_sync(void *handle)
{
    assert(handle == disk);
    syncs++;
    return sync_error || (fail_sync_at && syncs == fail_sync_at);
}

static void preference_close(void *handle)
{
    assert(handle == disk);
    storage_closes++;
}

#define osos_file_open preference_open
#define osos_file_read preference_read
#define osos_file_write preference_write
#define osos_file_set_size preference_set_size
#define osos_file_sync preference_sync
#define osos_file_close preference_close
#include "../../../payload/theme_preferences.c"
#include "../../../payload/theme_discovery.h"

int cfw_theme_discovery_path(const char *id, const char *file, struct osos_path *out)
{
    assert(!strcmp(id, "dark") && !strcmp(file, "theme.bin"));
    out->volume = 0;
    strcpy(out->text, "themes/dark/theme.bin");
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

static int preference_delete(const struct osos_path *path)
{
    (void)path;
    assert(0);
    return 0;
}

#define osos_file_delete preference_delete
#include "../../../payload/preferences.c"
#undef osos_file_delete
#include "../../../payload/theme_boot.c"
#undef osos_file_open
#undef osos_file_read
#undef osos_file_write
#undef osos_file_set_size
#undef osos_file_sync
#undef osos_file_close

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
    assert(
        cfw_theme_boot_selection(CFW_THEME_PREF_VALID, &saved, 0x314b5244, &desired));
    assert(!desired.id[0] && !desired.dark);
}

static void storage(void)
{
    struct cfw_theme_preferences state = {1, 0, {"modern", 1, 0}}, saved;
    open_error = -43;
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_ABSENT);
    open_error = -36;
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_INVALID);
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_UNKNOWN);
    open_error = 0;
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_INVALID);
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_DURABLE);
    assert(syncs == 2);
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_VALID);
    assert(!memcmp(&saved, &state, sizeof(state)));
    /* Interrupted pending writes never activate an unconfirmed new selection. */
    for (unsigned n = 0; n < CFW_THEME_PREFERENCE_BYTES; n++) {
        state.pending = 0;
        write_limit = CFW_THEME_PREFERENCE_BYTES;
        assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_DURABLE);
        state.pending = 1;
        write_limit = n;
        assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_UNKNOWN);
        enum cfw_theme_preferences_status status = cfw_theme_preferences_load(&saved);
        struct cfw_theme_selection desired;
        int recover = cfw_theme_boot_selection(status, &saved, 0, &desired);
        /* A write that changed no bytes can preserve the prior confirmed theme. */
        assert(recover ? !desired.id[0] && !desired.dark
                       : !strcmp(desired.id, "modern") && desired.dark);
    }
    write_limit = CFW_THEME_PREFERENCE_BYTES;
    sync_error = 1;
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_UNKNOWN);
    sync_error = 0;
    fail_sync_at = syncs + 2;
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_UNKNOWN);
    fail_sync_at = 0;
    write_error = -36;
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_UNKNOWN);
    write_error = 0;
    read_error = -36;
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_UNKNOWN);
    read_error = 0;
    corrupt_readback = 1;
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_UNKNOWN);
    corrupt_readback = 0;
    disk_size++;
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_INVALID);
    size_error = 1;
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_UNKNOWN);
    size_error = 0;
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_DURABLE);
    assert(disk_size == CFW_THEME_PREFERENCE_BYTES);
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_VALID);
    state.version = 2;
    unsigned before = storage_opens;
    assert(cfw_theme_preferences_store(&state) == CFW_THEME_STORE_UNCHANGED);
    assert(storage_opens == before && storage_opens == storage_closes);
}

static enum cfw_theme_store_result persist(void *context,
                                           const struct cfw_theme_preferences *state)
{
    (void)context;
    return cfw_theme_preferences_store(state);
}

static void switch_storage_ordering(void)
{
    const struct cfw_theme_switch_ops ops = {persist};
    struct cfw_theme_switch switcher;
    struct cfw_theme_preferences saved;
    const struct cfw_theme_selection modern = {"modern", 1, 0}, classic = {"", 0, 0};
    const struct osos_path pack = {0, "0"}, baseline = {0, "1"};
    disk_size = sizeof(disk);
    assert(cfw_theme_switch_init(&switcher, &ops, NULL));
    assert(cfw_theme_switch_store_classic(&switcher));
    assert(disk_size == CFW_THEME_PREFERENCE_BYTES);
    assert(cfw_theme_switch_apply(&switcher, &modern, &pack, &baseline));
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_VALID);
    assert(!saved.pending && !strcmp(saved.selection.id, "modern"));
    assert(!cfw_theme_switch_apply(&switcher, &classic, NULL, &baseline));
    cfw_theme_input_drained();
    sync_error = 1;
    assert(!cfw_theme_switch_apply(&switcher, &classic, NULL, &baseline));
    assert(!strcmp(switcher.active.selection.id, "modern"));
    assert(!allocations && !live_bytes && storage_opens == storage_closes);
    sync_error = 0;
    cfw_theme_input_drained();
    assert(cfw_theme_switch_apply(&switcher, &classic, NULL, &baseline));
    assert(cfw_theme_preferences_load(&saved) == CFW_THEME_PREF_VALID);
    assert(!saved.pending && !saved.selection.id[0]);
}

static void boot_resolution(void)
{
    struct cfw_theme_boot_request request;
    struct cfw_theme_preferences state = {1, 0, {"modern", 1, 0}};
    unsigned sync_count = syncs;
    open_error = -43;
    legacy_record = 0x314b5244;
    assert(!cfw_theme_boot_resolve(&request));
    assert(legacy_reads == 1 && !metadata_reads);
    assert(!request.selection.id[0] && request.selection.dark);
    assert(request.appearance_mask == 3);
    assert(!strcmp(request.pack.text, "themes/dark/theme.bin"));
    legacy_error = -1;
    assert(!cfw_theme_boot_resolve(&request));
    assert(!request.selection.dark && !request.pack.text[0]);
    legacy_error = 0;
    unsigned reads = legacy_reads;
    open_error = -1;
    assert(cfw_theme_boot_resolve(&request));
    assert(legacy_reads == reads && !request.selection.dark);
    open_error = 0;
    disk_size = CFW_THEME_PREFERENCE_BYTES;
    assert(cfw_theme_preferences_encode(&state, disk));
    assert(!cfw_theme_boot_resolve(&request));
    assert(legacy_reads == reads && metadata_reads == 1);
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
    assert(cfw_theme_preferences_encode(&state, disk));
    assert(cfw_theme_boot_resolve(&request));
    assert(metadata_reads == count && legacy_reads == reads);
    disk[0] ^= 1;
    assert(cfw_theme_boot_resolve(&request));
    assert(metadata_reads == count && legacy_reads == reads);
    assert(!request.pack.text[0] && !request.selection.id[0]);
    assert(syncs == sync_count && storage_opens == storage_closes);
}

int main(void)
{
    memcpy(original, pixels, sizeof(original));
    make_target(0);
    make_baseline();
    fix_crc();
    codec();
    storage();
    switch_storage_ordering();
    boot_resolution();
    return 0;
}
