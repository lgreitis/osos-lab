/* SPDX-License-Identifier: GPL-3.0-only */

#include "custom_eq.h"
#include "osos.h"
#include "patch.h"

PATCH_CALL(0x080587EC, 0x0809C5E8, cfw_preferences_hook);
PATCH_POINTER(0x0899BF4C, 0x0821C690, cfw_settings_action);
PATCH_POINTER(0x0899FE64, 0x0821C690, cfw_settings_action);
PATCH_JUMP(0x080B27F4, 0xE2400064, 0xE3500015, cfw_eq_map_hook);
PATCH_CALL_WORD(0x081732B4, 0xE350006B, cfw_eq_track_guard);

/* Native preset index 23 / saved ID 122. Reuse Flat's preview. */
const struct {
    uint32_t name, preview;
} cfw_eq_presets[24] = {
    [23] = {CFW_EQ_PRESET_NAME, 0},
};

const uint32_t cfw_eq_preset_ids[24] = {[23] = 122};
PATCH_COPY(cfw_eq_presets, 0, 0x089CC500, 23 * 8);
PATCH_COPY(cfw_eq_presets, 23 * 8 + 4, 0x089CC500 + 8 * 8 + 4, 4);
PATCH_COPY(cfw_eq_preset_ids, 0, 0x083E2568, 23 * 4);
PATCH_POINTER(0x0807BF14, 0x083E2568, cfw_eq_preset_ids);
PATCH_POINTER(0x080BB408, 0x083E2568, cfw_eq_preset_ids);
PATCH_POINTER(0x081E01A0, 0x089CC500, cfw_eq_presets);
PATCH_POINTER(0x081E01E8, 0x089CC500, cfw_eq_presets);
PATCH_POINTER(0x08293CF4, 0x089CC500, cfw_eq_presets);

/* Preset provider counts and lookup bounds: 23 -> 24 entries. */
PATCH_WORD(0x080BB3F4, 0xE3520017, 0xE3520018);
PATCH_WORD(0x081E016C, 0xE3510017, 0xE3510018);
PATCH_WORD(0x081E01B8, 0xE3510017, 0xE3510018);
PATCH_WORD(0x081E0CC0, 0xE3A00017, 0xE3A00018);
PATCH_WORD(0x08293B38, 0xE3550017, 0xE3550018);

static uint32_t settings[CFW_EQ_FIELDS];
static int initialized;
static unsigned int save_status;
static const struct osos_path settings_path = {0, "iPod_Control\\Device\\CFWEQ"};

struct eq_file {
    uint32_t magic, version;
    uint32_t values[CFW_EQ_FIELDS];
    uint32_t checksum;
};

static int equal(const char *a, const char *b)
{
    if (!a)
        return 0;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

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
}

void cfw_eq_load_preferences(void)
{
    struct eq_file file;
    void *handle;
    uint32_t count = 0;
    defaults();
    if (osos_file_open(&settings_path, 1, &handle) != 0)
        return;
    int result = osos_file_read(handle, &file, sizeof(file), &count);
    osos_file_close(handle);
    if (result || count != sizeof(file) || file.magic != 0x51455743 ||
        file.version != 1 || file.checksum != checksum(&file))
        return;
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
    void *handle;
    uint32_t count = 0;
    file.magic = 0x51455743;
    file.version = 1;
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        file.values[i] = settings[i];
    file.checksum = checksum(&file);
    save_status = 1;
    if (osos_file_open(&settings_path, 2, &handle) != 0)
        return;
    int result = osos_file_write(handle, &file, sizeof(file), &count);
    osos_file_close(handle);
    if (!result && count == sizeof(file))
        save_status = 0;
}

PATCH_ARM int cfw_settings_action(void *controller, const char *action,
                                  uint32_t argument)
{
    unsigned int field, value;
    int open = equal(action, "CFW_EQ_Open");
    int save = equal(action, "CFW_EQ_Save");
    int chosen = cfw_ui_selection("CFW_EQ_Set_", action, cfw_eq_fields, CFW_EQ_FIELDS,
                                  &field, &value);
    if (open || save || chosen) {
        if (argument == 0xdeadbeef)
            return 1;
        if (!initialized)
            defaults();
        if (chosen) {
            unsigned int previous = settings[field];
            settings[field] = value;
            if (cfw_eq_publish(settings)) {
                save_preferences();
                cfw_ui_refresh(&cfw_eq_fields[field], value);
                osos_menu_item_changed(cfw_eq_fields[field].items[previous]);
                osos_menu_item_changed(cfw_eq_fields[field].items[value]);
            } else {
                settings[field] = previous;
                save_status = 2;
            }
        }
        if (save)
            save_preferences();
        if (open)
            for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
                cfw_ui_refresh(&cfw_eq_fields[i], settings[i]);
        cfw_ui_status(&cfw_eq_save, save_status);
        return 1;
    }
    return osos_settings_action(controller, action, argument);
}
