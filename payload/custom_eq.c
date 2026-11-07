/* SPDX-License-Identifier: GPL-3.0-only */

#include "custom_eq.h"
#include "osos.h"
#include "cfw.h"
#include "preferences.h"
#include "store.h"
#include "patch.h"

/* Native preset index 23 / saved ID 122. Reuse Flat's preview. */
const struct {
    uint32_t name, preview;
} cfw_eq_presets[24] = {
    [23] = {CFW_EQ_PRESET_NAME, 0},
};

const uint32_t cfw_eq_preset_ids[24] = {[23] = 122};

PATCH_COPY(cfw_eq_presets, 0, OSOS_EQ_PRESETS, 23 * 8);
PATCH_COPY(cfw_eq_presets, 23 * 8 + 4, OSOS_EQ_PRESETS + 8 * 8 + 4, 4);
PATCH_COPY(cfw_eq_preset_ids, 0, OSOS_EQ_PRESET_IDS, 23 * 4);

static uint32_t settings[CFW_EQ_FIELDS];
static int initialized;
static unsigned int save_status;

/* Legacy import: CFWEQ file formats, versions 1-3. */
struct eq_file {
    uint32_t magic, version;
    uint32_t values[CFW_EQ_FIELDS];
    uint32_t checksum;
};

enum { EQ_LEGACY_FIELDS = 13 };

struct legacy_eq_file {
    uint32_t magic, version;
    uint32_t values[EQ_LEGACY_FIELDS];
    uint32_t checksum;
};

static uint32_t checksum(const void *file, unsigned int size)
{
    const unsigned char *bytes = (const unsigned char *)file;
    uint32_t hash = 2166136261u;
    for (unsigned int i = 0; i < size - 4; i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static void defaults(void)
{
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        settings[i] = cfw_eq_fields[i].default_value;
    cfw_eq_publish(settings);
    initialized = 1;
}

/* Legacy import: reads the pre-store CFWEQ file. */
static int load_legacy(uint32_t values[CFW_EQ_FIELDS])
{
    struct eq_file file;
    if (!cfw_preferences_read(CFW_EQ_LEGACY_FILE, &file, 8) || file.magic != 0x51455743)
        return 0;
    if (file.version == 1 || file.version == 2) {
        struct legacy_eq_file legacy;
        if (!cfw_preferences_read(CFW_EQ_LEGACY_FILE, &legacy, sizeof(legacy)) ||
            legacy.checksum != checksum(&legacy, sizeof(legacy)))
            return 0;
        if (legacy.version == 1) {
            if (legacy.values[CFW_EQ_PRECUT] > 20)
                return 0;
            legacy.values[CFW_EQ_PRECUT] *= 5;
        }
        for (unsigned int i = 0; i < EQ_LEGACY_FIELDS; i++)
            values[i] = legacy.values[i];
        return 1;
    }
    if (file.version != 3 ||
        !cfw_preferences_read(CFW_EQ_LEGACY_FILE, &file, sizeof(file)) ||
        file.checksum != checksum(&file, sizeof(file)))
        return 0;
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        values[i] = file.values[i];
    return 1;
}

void cfw_eq_load_preferences(void)
{
    uint32_t values[CFW_EQ_FIELDS];
    defaults();
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        values[i] = settings[i];
    /* Fields only append: a shorter record comes from an older build and the rest keep
     * defaults; a longer one comes from a newer build and its extra fields are ignored. */
    int stored = cfw_store_get(CFW_STORE_CUSTOM_EQ, values, sizeof(values)) != 0;
    if (!stored && !(cfw_store_migrating() && load_legacy(values)))
        return;
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        if (values[i] >= cfw_eq_fields[i].count)
            return;
    if (!cfw_eq_publish(values))
        return;
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        settings[i] = values[i];
    if (!stored)
        cfw_store_set(CFW_STORE_CUSTOM_EQ, settings, sizeof(settings));
}

static void change_value(uint32_t field, uint32_t value)
{
    if (!initialized)
        defaults();
    if (field >= CFW_EQ_FIELDS || value >= cfw_eq_fields[field].count ||
        settings[field] == value)
        return;
    uint32_t previous = settings[field];
    settings[field] = value;
    if (cfw_eq_publish(settings)) {
        save_status = 0;
        cfw_ui_refresh(&cfw_eq_fields[field]);
    } else {
        settings[field] = previous;
        save_status = 1;
    }
    cfw_ui_status(&cfw_eq_save, save_status);
}

/* The store ignores unchanged bytes, so commits need no dirty tracking. */
static void commit_values(void)
{
    cfw_store_set(CFW_STORE_CUSTOM_EQ, settings, sizeof(settings));
}

const struct cfw_ui_model cfw_eq_model = {settings, change_value, commit_values};

int cfw_eq_action(const char *action, uint32_t argument)
{
    int open = cfw_string_equal(action, "CFW_EQ_Open");
    int save = cfw_string_equal(action, "CFW_EQ_Save");
    if (!open && !save)
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    if (!initialized)
        defaults();
    if (save)
        commit_values();
    if (open)
        for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
            cfw_ui_refresh(&cfw_eq_fields[i]);
    cfw_ui_status(&cfw_eq_save, save_status);
    return 1;
}
