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

static uint32_t visible[2];
const struct cfw_ui_visibility cfw_ui_visibility[] = {
    {101, 1, {11, 21}, {2, 3}, 2, "iPod_Control\\Device\\CFWMenu_a", &visible[0]},
    {202, 2, {12, 22}, {2, 3}, 1, "iPod_Control\\Device\\CFWMenu_b", &visible[1]},
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

static void check_records(void)
{
    uint32_t value = 0, pair[4];
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
    assert(dirty);
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

    /* Resizing moves the record behind the others; unknown records are kept. */
    pair[0] = 1;
    pair[1] = 2;
    cfw_store_set(1, pair, 8);
    cfw_store_set(3, "x", 1);
    cfw_store_flush();
    boot();
    assert(cfw_store_get(2, text, sizeof(text)) == 5 && !strcmp(text, "abcde"));
    assert(cfw_store_get(1, pair, sizeof(pair)) == 8 && pair[1] == 2);
    assert(cfw_store_get(3, text, 1) == 1 && text[0] == 'x');

    /* A shorter rewrite leaves the old tail in the file; the header bounds it. */
    cfw_store_set(1, &value, 4);
    uint32_t old_size = store_file()->size;
    cfw_store_flush();
    assert(store_file()->size == old_size && store[2] < old_size);
    boot();
    assert(!migrating && cfw_store_get(1, &value, 4) == 4);

    /* Failed writes keep the store dirty and retry on the next flush. */
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

    /* Records that do not fit are not stored. */
    static uint32_t big[CAPACITY];
    cfw_store_set(4, big, sizeof(big));
    assert(!cfw_store_get(4, big, sizeof(big)) && cfw_store_get(1, &value, 4) == 4);

    /* Damaged files start a new migration. */
    store_file()->data[16] ^= 1;
    boot();
    assert(migrating && !cfw_store_get(1, &value, 4));
    uint32_t bad[8] = {MAGIC, VERSION, 32, 0, 1, 0x7fffffff, 0, 0};
    bad[3] = 2166136261u;
    for (unsigned i = 16; i < 32; i++)
        bad[3] = (bad[3] ^ ((unsigned char *)bad)[i]) * 16777619u;
    fake_put(STORE_FILE, bad, sizeof(bad));
    boot();
    assert(migrating);
    bad[2] = 64;
    fake_put(STORE_FILE, bad, sizeof(bad));
    boot();
    assert(migrating);

    /* Migration deletes legacy files only after a successful write. */
    fake_fail_write = 1;
    cfw_store_flush();
    assert(migrating && legacy_deletions == 1);
    fake_fail_write = 0;
    cfw_store_flush();
    assert(!migrating && legacy_deletions == 2);
}

static void check_visibility(void)
{
    struct fake_file *file = store_file();
    file->exists = 0;
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
    assert(!fake_find(cfw_ui_visibility[1].filename, 0));

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

    /* Entries missing from the record keep their defaults. */
    uint32_t pairs[2] = {202, 2};
    cfw_store_set(CFW_STORE_MENU_VISIBILITY, pairs, sizeof(pairs));
    cfw_store_flush();
    visible[0] = 2;
    boot();
    cfw_ui_visibility_load_preferences();
    assert(visible[0] == 2 && visible[1] == 2);
}

int main(void)
{
    check_records();
    check_visibility();
    return 0;
}
