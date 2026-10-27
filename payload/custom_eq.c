/* SPDX-License-Identifier: GPL-3.0-only */

#include "custom_eq.h"
#include "osos.h"
#include "cfw.h"
#include "preferences.h"
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
static int dirty;
static const struct osos_path settings_path = {0, "iPod_Control\\Device\\CFWEQ"};

struct eq_file {
    uint32_t magic, version;
    uint32_t values[CFW_EQ_FIELDS];
    uint32_t checksum;
};

static uint32_t checksum(const struct eq_file *file)
{
    const unsigned char *bytes = (const unsigned char *)file;
    uint32_t hash = 2166136261u;
    for (unsigned int i = 0; i < sizeof(*file) - 4; i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static void defaults(void)
{
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        settings[i] = cfw_eq_fields[i].default_value;
    cfw_eq_publish(settings);
    initialized = 1;
    dirty = 0;
}

void cfw_eq_load_preferences(void)
{
    struct eq_file file;
    defaults();
    if (!cfw_preferences_read(&settings_path, &file, sizeof(file)) ||
        file.magic != 0x51455743 || (file.version != 1 && file.version != 2) ||
        file.checksum != checksum(&file))
        return;
    if (file.version == 1) {
        if (file.values[CFW_EQ_PRECUT] > 20)
            return;
        file.values[CFW_EQ_PRECUT] *= 5;
    }
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        if (file.values[i] >= cfw_eq_fields[i].count)
            return;
    if (!cfw_eq_publish(file.values))
        return;
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        settings[i] = file.values[i];
}

static void save_preferences(void)
{
    struct eq_file file;
    file.magic = 0x51455743;
    file.version = 2;
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        file.values[i] = settings[i];
    file.checksum = checksum(&file);
    save_status = !cfw_preferences_write(&settings_path, &file, sizeof(file));
    if (!save_status)
        dirty = 0;
}

static void change_value(uint32_t field, uint32_t value)
{
    if (!initialized)
        defaults();
    uint32_t previous = settings[field];
    settings[field] = value;
    if (cfw_eq_publish(settings)) {
        dirty = 1;
        cfw_ui_refresh(&cfw_eq_fields[field]);
    } else {
        settings[field] = previous;
        save_status = 2;
    }
    cfw_ui_status(&cfw_eq_save, save_status);
}

static void commit_values(void)
{
    if (dirty) {
        save_preferences();
        cfw_ui_status(&cfw_eq_save, save_status);
    }
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
        save_preferences();
    if (open)
        for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
            cfw_ui_refresh(&cfw_eq_fields[i]);
    cfw_ui_status(&cfw_eq_save, save_status);
    return 1;
}
