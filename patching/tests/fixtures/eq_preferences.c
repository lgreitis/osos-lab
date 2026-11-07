/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_COPY(...)
#define OSOS_ACTION_SUPPORT_QUERY 0xdeadbeef
#include "../../../payload/custom_eq.h"

struct osos_path {
    uint16_t volume;
    char text[258];
};

static uint32_t status, refreshes;
static int filter_failure;
const struct cfw_ui_field cfw_eq_fields[CFW_EQ_FIELDS] = {
    {.count = 101},
    {.count = 4},
    {.count = 32},
    {.count = 38},
    {.count = 49},
    {.count = 4},
    {.count = 32},
    {.count = 38},
    {.count = 49},
    {.count = 4},
    {.count = 32},
    {.count = 38},
    {.count = 49},
    {.count = 4},
    {.count = 32, .default_value = 12},
    {.count = 38, .default_value = 4},
    {.count = 49, .default_value = 24},
    {.count = 4},
    {.count = 32, .default_value = 24},
    {.count = 38, .default_value = 4},
    {.count = 49, .default_value = 24},
};
const struct cfw_ui_action cfw_eq_save = {.count = 3};

#include "fake_files.h"

int cfw_eq_publish(const uint32_t values[CFW_EQ_FIELDS])
{
    for (unsigned i = 0; i < CFW_EQ_FIELDS; i++)
        assert(values[i] < cfw_eq_fields[i].count);
    return !filter_failure;
}

void cfw_ui_refresh(const struct cfw_ui_field *field)
{
    assert(field >= cfw_eq_fields && field < cfw_eq_fields + CFW_EQ_FIELDS);
    refreshes++;
}

void cfw_ui_status(const struct cfw_ui_action *action, unsigned value)
{
    assert(action == &cfw_eq_save && value < 3);
    status = value;
}

#include "../../../payload/preferences.c"

void cfw_store_delete_legacy(void)
{
    cfw_preferences_delete(CFW_EQ_LEGACY_FILE);
}

#define checksum store_checksum
#include "../../../payload/store.c"
#undef checksum
#include "../../../payload/custom_eq.c"

#define STORE_FILE "iPod_Control\\Device\\CFWPreferences"

static void boot(void)
{
    dirty = migrating = 0;
    cfw_store_load();
    cfw_eq_load_preferences();
    cfw_store_flush();
}

static void legacy(uint32_t version, uint32_t precut)
{
    struct legacy_eq_file file = {.magic = 0x51455743, .version = version};
    file.values[0] = precut;
    file.values[4] = 24;
    file.checksum = checksum(&file, sizeof(file));
    fake_put(CFW_EQ_LEGACY_FILE, &file, sizeof(file));
}

static void fresh_device(void)
{
    struct fake_file *file = fake_find(STORE_FILE, 0);
    if (file)
        file->exists = 0;
}

int main(void)
{
    /* Migration: a v1 file is imported, written to the store and deleted. */
    legacy(1, 7);
    boot();
    assert(settings[0] == 35 && settings[4] == 24);
    assert(fake_writes == 1 && fake_find(STORE_FILE, 0));
    assert(!fake_find(CFW_EQ_LEGACY_FILE, 0) && !migrating);
    boot();
    assert(settings[0] == 35 && fake_writes == 1);

    /* With a store present legacy files are ignored and kept. */
    legacy(2, 37);
    boot();
    assert(settings[0] == 35 && fake_find(CFW_EQ_LEGACY_FILE, 0));

    fresh_device();
    legacy(1, 21);
    boot();
    assert(settings[0] == 0 && !fake_find(CFW_EQ_LEGACY_FILE, 0));
    fresh_device();
    legacy(2, 37);
    boot();
    assert(settings[0] == 37);
    assert(settings[CFW_EQ_BAND4_TYPE] == 0 && settings[CFW_EQ_BAND5_TYPE] == 0);
    assert(settings[CFW_EQ_BAND4_GAIN] == 24 && settings[CFW_EQ_BAND5_GAIN] == 24);
    assert(settings[CFW_EQ_BAND4_FREQUENCY] == 12 &&
           settings[CFW_EQ_BAND5_FREQUENCY] == 24);
    fresh_device();
    struct eq_file v3 = {.magic = 0x51455743, .version = 3};
    v3.values[0] = 40;
    v3.values[CFW_EQ_BAND5_GAIN] = 30;
    v3.checksum = checksum(&v3, sizeof(v3));
    fake_put(CFW_EQ_LEGACY_FILE, &v3, sizeof(v3));
    boot();
    assert(settings[0] == 40 && settings[CFW_EQ_BAND5_GAIN] == 30);
    fresh_device();
    v3.values[1] = 4;
    v3.checksum = checksum(&v3, sizeof(v3));
    fake_put(CFW_EQ_LEGACY_FILE, &v3, sizeof(v3));
    boot();
    assert(settings[0] == 0 && settings[CFW_EQ_BAND5_GAIN] == 24);

    /* Edits stay in RAM until the store flushes. */
    unsigned writes = fake_writes;
    assert(cfw_eq_action("CFW_EQ_Save", OSOS_ACTION_SUPPORT_QUERY));
    cfw_eq_model.change(0, 38);
    assert(settings[0] == 38 && refreshes == 1);
    cfw_eq_model.change(CFW_EQ_FIELDS, 0);
    cfw_eq_model.change(0, 101);
    cfw_eq_model.change(0, 38);
    assert(refreshes == 1 && settings[0] == 38);
    cfw_eq_model.commit();
    assert(dirty && fake_writes == writes);
    cfw_eq_action("CFW_EQ_Open", 0);
    assert(status == 0);
    boot();
    assert(settings[0] == 0 && fake_writes == writes);
    cfw_eq_model.change(0, 38);
    cfw_eq_model.commit();
    cfw_store_flush();
    assert(fake_writes == writes + 1);
    boot();
    assert(settings[0] == 38);

    /* Rejected filters report status 1 and keep the published values. */
    filter_failure = 1;
    cfw_eq_model.change(0, 39);
    assert(settings[0] == 38 && status == 1);
    filter_failure = 0;
    cfw_eq_model.change(0, 39);
    assert(status == 0);
    cfw_eq_action("CFW_EQ_Save", 0);
    assert(dirty);
    fake_fail_write = 1;
    cfw_store_flush();
    assert(dirty);
    fake_fail_write = 0;
    cfw_store_flush();
    boot();
    assert(settings[0] == 39);

    /* A record from a build with fewer fields keeps defaults for the rest. */
    uint32_t short_record[EQ_LEGACY_FIELDS] = {50};
    cfw_store_set(CFW_STORE_CUSTOM_EQ, short_record, sizeof(short_record));
    cfw_store_flush();
    boot();
    assert(settings[0] == 50 && settings[CFW_EQ_BAND4_FREQUENCY] == 12);

    /* A record from a build with more fields keeps the known prefix. */
    uint32_t long_record[CFW_EQ_FIELDS + 2] = {45};
    for (unsigned int i = 1; i < CFW_EQ_FIELDS; i++)
        long_record[i] = settings[i];
    long_record[CFW_EQ_FIELDS] = 99;
    cfw_store_set(CFW_STORE_CUSTOM_EQ, long_record, sizeof(long_record));
    cfw_store_flush();
    boot();
    assert(settings[0] == 45 && settings[CFW_EQ_BAND4_FREQUENCY] == 12);

    /* A corrupted store falls back to defaults. */
    fake_find(STORE_FILE, 0)->data[20] ^= 1;
    boot();
    assert(settings[0] == 0 && !migrating);
    return 0;
}
