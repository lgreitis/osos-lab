/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_COPY(...)
#include "../../../payload/custom_eq.h"

struct osos_path {
    uint16_t volume;
    char text[258];
};

static int filter_failure;
const struct cfw_ui_field cfw_eq_fields[CFW_EQ_FIELDS] = {
    {.count = 102},
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

#include "fake_files.h"

int cfw_eq_publish(const uint32_t values[CFW_EQ_FIELDS])
{
    for (unsigned i = 0; i < CFW_EQ_FIELDS; i++)
        assert(values[i] < cfw_eq_fields[i].count);
    return !filter_failure;
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
    /* Migration: a v1 file is imported, written to the store and deleted. Files
     * before v4 index manual precuts from 0 dB, so they move past Auto. */
    legacy(1, 7);
    boot();
    assert(settings[0] == 36 && settings[4] == 24);
    assert(fake_writes == 1 && fake_find(STORE_FILE, 0));
    assert(!fake_find(CFW_EQ_LEGACY_FILE, 0) && !migrating);
    boot();
    assert(settings[0] == 36 && fake_writes == 1);

    /* With a store present legacy files are ignored and kept. */
    legacy(2, 37);
    boot();
    assert(settings[0] == 36 && fake_find(CFW_EQ_LEGACY_FILE, 0));

    fresh_device();
    legacy(1, 21);
    boot();
    assert(settings[0] == CFW_EQ_PRECUT_AUTO && !fake_find(CFW_EQ_LEGACY_FILE, 0));
    fresh_device();
    legacy(2, 37);
    boot();
    assert(settings[0] == 38);
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
    assert(settings[0] == 41 && settings[CFW_EQ_BAND5_GAIN] == 30);
    fresh_device();
    v3.values[1] = 4;
    v3.checksum = checksum(&v3, sizeof(v3));
    fake_put(CFW_EQ_LEGACY_FILE, &v3, sizeof(v3));
    boot();
    assert(settings[0] == CFW_EQ_PRECUT_AUTO && settings[CFW_EQ_BAND5_GAIN] == 24);
    /* v4 files already include Auto. */
    fresh_device();
    struct eq_file v4 = {.magic = 0x51455743, .version = 4};
    v4.values[0] = 40;
    v4.values[CFW_EQ_BAND5_GAIN] = 30;
    v4.checksum = checksum(&v4, sizeof(v4));
    fake_put(CFW_EQ_LEGACY_FILE, &v4, sizeof(v4));
    boot();
    assert(settings[0] == 40 && settings[CFW_EQ_BAND5_GAIN] == 30);

    /* Edits stay in RAM until the store flushes. */
    unsigned writes = fake_writes;
    cfw_eq_model.change(0, 38);
    assert(settings[0] == 38);
    cfw_eq_model.change(CFW_EQ_FIELDS, 0);
    cfw_eq_model.change(0, 102);
    assert(settings[0] == 38);
    cfw_eq_model.commit();
    assert(dirty && fake_writes == writes);
    boot();
    assert(settings[0] == 40 && fake_writes == writes);
    cfw_eq_model.change(0, 38);
    cfw_eq_model.commit();
    cfw_store_flush();
    assert(fake_writes == writes + 1);
    boot();
    assert(settings[0] == 38);

    /* Rejected filters keep the published values; failed writes retry. */
    filter_failure = 1;
    cfw_eq_model.change(0, 39);
    assert(settings[0] == 38);
    filter_failure = 0;
    cfw_eq_model.change(0, 39);
    cfw_eq_model.commit();
    assert(dirty);
    fake_fail_write = 1;
    cfw_store_flush();
    assert(dirty);
    fake_fail_write = 0;
    cfw_store_flush();
    boot();
    assert(settings[0] == 39);

    /* A record from before Auto precut moves past Auto once and is rewritten. */
    uint32_t old_record[CFW_EQ_FIELDS];
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        old_record[i] = settings[i];
    old_record[0] = 20;
    cfw_store_set(CFW_STORE_CUSTOM_EQ, old_record, sizeof(old_record));
    cfw_store_flush();
    boot();
    assert(settings[0] == 21);
    struct eq_record saved;
    assert(cfw_store_get(CFW_STORE_CUSTOM_EQ, &saved, sizeof(saved)) == sizeof(saved));
    assert(saved.values[0] == 21 && saved.layout == EQ_LAYOUT_AUTO_PRECUT);
    boot();
    assert(settings[0] == 21);

    /* A record from a build with fewer fields keeps defaults for the rest. */
    uint32_t short_record[EQ_LEGACY_FIELDS] = {50};
    cfw_store_set(CFW_STORE_CUSTOM_EQ, short_record, sizeof(short_record));
    cfw_store_flush();
    boot();
    assert(settings[0] == 51 && settings[CFW_EQ_BAND4_FREQUENCY] == 12);

    /* A record from a build with more fields keeps the known prefix. */
    uint32_t long_record[CFW_EQ_FIELDS + 2] = {45};
    for (unsigned int i = 1; i < CFW_EQ_FIELDS; i++)
        long_record[i] = settings[i];
    long_record[CFW_EQ_FIELDS] = EQ_LAYOUT_AUTO_PRECUT;
    long_record[CFW_EQ_FIELDS + 1] = 99;
    cfw_store_set(CFW_STORE_CUSTOM_EQ, long_record, sizeof(long_record));
    cfw_store_flush();
    boot();
    assert(settings[0] == 45 && settings[CFW_EQ_BAND4_FREQUENCY] == 12);

    /* Reset All Settings stores the defaults and keeps other records. */
    const uint32_t other = 0x12345678, other_key = 0x4b4e554b;
    cfw_store_set(other_key, &other, sizeof(other));
    cfw_store_flush();
    cfw_eq_reset_preferences();
    assert(settings[0] == CFW_EQ_PRECUT_AUTO && settings[CFW_EQ_BAND4_FREQUENCY] == 12);
    assert(dirty);
    cfw_store_flush();
    boot();
    assert(settings[0] == CFW_EQ_PRECUT_AUTO && settings[CFW_EQ_BAND4_FREQUENCY] == 12);
    uint32_t kept = 0;
    assert(cfw_store_get(other_key, &kept, sizeof(kept)) == 4 && kept == other);

    /* A corrupted store falls back to defaults. */
    fake_find(STORE_FILE, 0)->data[20] ^= 1;
    boot();
    assert(settings[0] == CFW_EQ_PRECUT_AUTO && !migrating);
    return 0;
}
