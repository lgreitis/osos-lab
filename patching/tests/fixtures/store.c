/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <string.h>
/* Native layout assertions assume 32-bit pointers; this fixture runs on the host. */
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert
#define CFW_PATCH_H
#define PATCH_ARM
#include "fake_files.h"

#define STORE_FILE "iPod_Control\\Device\\CFWPreferences"
#define BACKUP_FILE STORE_FILE ".bak"

static unsigned legacy_deletions, visible_calls, notifications, resets;
static uint32_t shown[2][4];

static int set_visible(uint32_t item, uint32_t visible)
{
    shown[item / 10 - 1][item % 10] = visible;
    visible_calls++;
    return 1;
}

static void notify(void *model, uint32_t type, uint32_t property)
{
    (void)model;
    (void)type;
    (void)property;
    notifications++;
}

void *cfw_main_menu_reset_original(void *model)
{
    resets++;
    return model;
}

void *cfw_music_menu_reset_original(void *model)
{
    resets++;
    return model;
}

static const uint32_t native_rows_table[5];
#undef OSOS_MAIN_MENU_SETTINGS_ROWS
#undef OSOS_MUSIC_MENU_SETTINGS_ROWS
#define OSOS_MAIN_MENU_SETTINGS_ROWS ((uintptr_t)native_rows_table)
#define OSOS_MUSIC_MENU_SETTINGS_ROWS ((uintptr_t)native_rows_table)
#define osos_menu_item_set_visible set_visible
#define osos_model_notify notify
#include "../../../payload/preferences.c"
#include "../../../payload/store.c"
#include "../../../payload/ui_visibility.c"
#include "legacy_visibility.h"

static uint32_t visible[2], available[2] = {3, 3};
const struct cfw_ui_visibility cfw_ui_visibility[] = {
    {101,
     1,
     {11, 21},
     {2, 3},
     2,
     "iPod_Control\\Device\\CFWMenu_a",
     &visible[0],
     &available[0]},
    {202,
     2,
     {12, 22},
     {2, 3},
     1,
     "iPod_Control\\Device\\CFWMenu_b",
     &visible[1],
     &available[1]},
};
const uint32_t cfw_ui_visibility_count = 2;

void cfw_store_delete_legacy(void)
{
    legacy_deletions++;
    cfw_ui_visibility_delete_legacy();
}

static void boot(void)
{
    dirty = migrating = 0;
    cfw_store_load();
}

static struct fake_file *store_file(void)
{
    return fake_find(STORE_FILE, 0);
}

static void empty_disk(void)
{
    memset(fake_files, 0, sizeof(fake_files));
    fake_fail_write = 0;
    fake_partial_write = -1;
    fake_fail_read_size = 0;
    fake_fail_open_read = fake_fail_read = fake_write_fault_name = NULL;
    fake_writes = fake_deletes = legacy_deletions = 0;
}

static void check_records(void)
{
    uint32_t value = 0, pair[4];
    empty_disk();
    boot();
    assert(migrating && dirty && !cfw_store_get(1, &value, 4));
    cfw_store_flush();
    assert(!migrating && !dirty && legacy_deletions == 1 && fake_writes == 1);
    assert(store_file()->size == 16);
    cfw_store_flush();
    assert(fake_writes == 1);

    value = 7;
    cfw_store_set(1, &value, 4);
    cfw_store_set(2, "abcde", 5);
    cfw_store_flush();
    cfw_store_set(1, &value, 4);
    assert(!dirty);
    boot();
    assert(!migrating && !dirty);
    char text[8] = {0};
    assert(cfw_store_get(2, text, sizeof(text)) == 5 && !strcmp(text, "abcde"));
    memset(text, 0, sizeof(text));
    assert(cfw_store_get(2, text, 4) == 4 && !strcmp(text, "abcd"));
    assert(cfw_store_get(1, &value, 4) == 4 && value == 7);

    pair[0] = 1;
    pair[1] = 2;
    cfw_store_set(1, pair, 8);
    cfw_store_set(3, "x", 1);
    cfw_store_flush();
    boot();
    assert(cfw_store_get(2, text, sizeof(text)) == 5 && !strcmp(text, "abcde"));
    assert(cfw_store_get(1, pair, sizeof(pair)) == 8 && pair[1] == 2);
    assert(cfw_store_get(3, text, 1) == 1 && text[0] == 'x');

    cfw_store_set(1, &value, 4);
    uint32_t old_size = store_file()->size;
    cfw_store_flush();
    assert(store_file()->size == old_size && store[2] < old_size);
    boot();
    assert(!migrating && cfw_store_get(1, &value, 4) == 4);

    value = 9;
    cfw_store_set(1, &value, 4);
    fake_fail_write = 1;
    cfw_store_flush();
    assert(dirty);
    fake_fail_write = 0;
    cfw_store_flush();
    assert(!dirty);
    boot();
    assert(cfw_store_get(1, &value, 4) == 4 && value == 9);

    static uint32_t big[CAPACITY];
    cfw_store_set(4, big, sizeof(big));
    cfw_store_set(4, big, UINT32_MAX);
    assert(!cfw_store_get(4, big, sizeof(big)) && cfw_store_get(1, &value, 4) == 4);
}

static void check_recovery(void)
{
    /* Interrupt either write at every byte; reboot must retain all saved records. */
    for (unsigned target = 0; target < 2; target++) {
        for (unsigned prefix = 0; prefix < 48; prefix++) {
            empty_disk();
            boot();
            uint32_t first = 7, unrelated = 13;
            cfw_store_set(1, &first, 4);
            cfw_store_set(2, &unrelated, 4);
            cfw_store_flush();
            uint32_t saved[CAPACITY];
            memcpy(saved, store, sizeof(store));
            first = 9;
            cfw_store_set(1, &first, 4);
            fake_write_fault_name = target ? STORE_FILE : BACKUP_FILE;
            fake_partial_write = prefix;
            cfw_store_flush();
            int complete = !dirty;
            fake_partial_write = -1;
            fake_write_fault_name = NULL;
            boot();
            assert(!blocked && !migrating);
            assert(cfw_store_get(1, &first, 4) == 4 && (first == 7 || first == 9));
            assert(!complete || first == 9);
            assert(cfw_store_get(2, &unrelated, 4) == 4 && unrelated == 13);
            if (first == 7)
                assert(!memcmp(store, saved, saved[2]));
            cfw_store_flush();
            boot();
            assert(!dirty && cfw_store_get(2, &unrelated, 4) == 4 && unrelated == 13);
        }
    }

    empty_disk();
    boot();
    uint32_t value = 7;
    cfw_store_set(1, &value, 4);
    cfw_store_flush();
    value = 9;
    cfw_store_set(1, &value, 4);
    cfw_store_flush();
    store_file()->data[16] ^= 1;
    boot();
    assert(dirty && !migrating && cfw_store_get(1, &value, 4) == 4 && value == 7);
    cfw_store_flush();
    boot();
    assert(!dirty && cfw_store_get(1, &value, 4) == 4 && value == 7);

    store_file()->exists = 0;
    boot();
    assert(dirty && !migrating && cfw_store_get(1, &value, 4) == 4 && value == 7);
    cfw_store_flush();
    assert(store_file());
}

static void check_read_failures(void)
{
    for (unsigned open_failure = 0; open_failure < 2; open_failure++) {
        empty_disk();
        boot();
        uint32_t value = 7;
        cfw_store_set(1, &value, 4);
        cfw_store_flush();
        unsigned char before[1024];
        memcpy(before, store_file()->data, sizeof(before));
        if (open_failure)
            fake_fail_open_read = STORE_FILE;
        else
            fake_fail_read = STORE_FILE;
        boot();
        assert(blocked);
        value = 99;
        cfw_store_set(1, &value, 4);
        unsigned writes = fake_writes, deletions = fake_deletes;
        cfw_store_flush();
        assert(dirty && fake_writes == writes && fake_deletes == deletions);
        assert(!memcmp(before, store_file()->data, sizeof(before)));
        fake_fail_open_read = fake_fail_read = NULL;
        boot();
        assert(!blocked && !dirty && cfw_store_get(1, &value, 4) == 4 && value == 7);
    }

    /* An unreadable backup must not be replaced with defaults after a torn primary. */
    uint32_t value = 9;
    cfw_store_set(1, &value, 4);
    cfw_store_flush();
    store_file()->data[16] ^= 1;
    fake_fail_read = BACKUP_FILE;
    boot();
    assert(blocked);
    unsigned writes = fake_writes;
    cfw_store_flush();
    assert(fake_writes == writes);
    fake_fail_read = NULL;
    boot();
    assert(!blocked && cfw_store_get(1, &value, 4) == 4 && value == 7);
    cfw_store_flush();

    /* A flush-time read failure leaves the valid primary untouched and retries. */
    value = 11;
    cfw_store_set(1, &value, 4);
    fake_fail_read = STORE_FILE;
    writes = fake_writes;
    cfw_store_flush();
    assert(dirty && fake_writes == writes);
    fake_fail_read = NULL;
    cfw_store_flush();
    boot();
    assert(cfw_store_get(1, &value, 4) == 4 && value == 11);

    empty_disk();
    uint32_t bad[8] = {MAGIC, VERSION, 32, 0, 1, 0x7fffffff, 0, 0};
    bad[3] = checksum(bad);
    fake_put(STORE_FILE, bad, sizeof(bad));
    boot();
    assert(migrating && !blocked);
    bad[2] = 64;
    fake_put(STORE_FILE, bad, sizeof(bad));
    boot();
    assert(migrating && !blocked);
}

static void check_visibility(void)
{
    empty_disk();
    struct cfw_ui_visibility_file legacy = cfw_ui_visibility_encode(101, 1);
    fake_put(cfw_ui_visibility[0].filename, &legacy, sizeof(legacy));
    legacy = cfw_ui_visibility_encode(202, 7);
    fake_put(cfw_ui_visibility[1].filename, &legacy, sizeof(legacy));
    visible[0] = 2;
    visible[1] = 1;
    boot();
    cfw_ui_visibility_load_preferences();
    assert(visible[0] == 1 && visible[1] == 1);
    cfw_store_flush();
    assert(!fake_find(cfw_ui_visibility[0].filename, 0));
    assert(fake_find(cfw_ui_visibility[1].filename, 0));

    unsigned writes = fake_writes;
    cfw_ui_music_menu_toggle(NULL, CFW_UI_CUSTOM_ROW | 1);
    assert(visible[1] == 3 && shown[1][2] == 1 && notifications == 1);
    assert(fake_writes == writes && dirty);
    cfw_store_flush();
    visible[0] = visible[1] = 0;
    boot();
    cfw_ui_visibility_load_preferences();
    assert(visible[0] == 1 && visible[1] == 3 && !migrating);

    cfw_ui_main_menu_reset(NULL);
    assert(visible[0] == 0 && visible[1] == 3 && resets == 1);
    cfw_store_flush();
    boot();
    cfw_ui_visibility_load_preferences();
    assert(visible[0] == 0 && visible[1] == 3);

    /* Runtime availability never overwrites the persisted user preference. */
    cfw_ui_visibility_set_available(12, 0);
    assert(shown[0][2] == 0 && visible[1] == 3);
    cfw_ui_main_menu_toggle(NULL, CFW_UI_CUSTOM_ROW | 1);
    assert(visible[1] == 2 && shown[0][2] == 0);
    cfw_ui_visibility_set_available(12, 1);
    assert(shown[0][2] == 0 && visible[1] == 2);
    cfw_store_flush();
    visible[1] = 3;
    boot();
    cfw_ui_visibility_load_preferences();
    assert(visible[1] == 2);
    cfw_ui_main_menu_reset(NULL);
    assert(visible[1] == 3 && shown[0][2] == 1);

    /* Entries missing from the record keep their defaults. */
    uint32_t pairs[2] = {202, 2};
    cfw_store_set(CFW_STORE_MENU_VISIBILITY, pairs, sizeof(pairs));
    cfw_store_flush();
    visible[0] = 2;
    boot();
    cfw_ui_visibility_load_preferences();
    assert(visible[0] == 2 && visible[1] == 2);
}

static void check_legacy_read_failure(void)
{
    empty_disk();
    struct cfw_ui_visibility_file legacy = cfw_ui_visibility_encode(101, 1);
    fake_put(cfw_ui_visibility[0].filename, &legacy, sizeof(legacy));
    legacy = cfw_ui_visibility_encode(202, 2);
    fake_put(cfw_ui_visibility[1].filename, &legacy, sizeof(legacy));
    fake_fail_read = cfw_ui_visibility[1].filename;
    visible[0] = 2;
    visible[1] = 1;
    boot();
    cfw_ui_visibility_load_preferences();
    assert(visible[0] == 1 && visible[1] == 1 && blocked);
    cfw_store_flush();
    assert(!fake_writes && !fake_deletes && !store_file());
    fake_fail_read = NULL;
    boot();
    cfw_ui_visibility_load_preferences();
    assert(visible[0] == 1 && visible[1] == 2 && !blocked);
    cfw_store_flush();
    assert(!fake_find(cfw_ui_visibility[0].filename, 0));
    assert(!fake_find(cfw_ui_visibility[1].filename, 0));
}

int main(void)
{
    check_records();
    check_recovery();
    check_read_failures();
    check_visibility();
    check_legacy_read_failure();
    return 0;
}
