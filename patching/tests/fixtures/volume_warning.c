/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "../../../payload/native/bindings.h"
static int native_region(void);
#undef OSOS_VOLUME_POLICY_REGION
#define OSOS_VOLUME_POLICY_REGION ((uintptr_t)native_region)
/* Native layout assertions assume 32-bit pointers; this fixture runs on the host. */
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert
#include "volume_warning_ui.h"
#define CFW_PATCH_H
#define PATCH_ARM

static uint32_t disk_record;
static int missing = 1, fail_write, writes, notifications, region = 1;
static int controller, model;

static int native_region(void)
{
    return region;
}

static int file_open(const struct osos_path *path, int mode, void **handle)
{
    assert(!strcmp(path->text, "iPod_Control\\Device\\CFWVolWarn"));
    if (mode == 1 && missing)
        return -1;
    *handle = &disk_record;
    return 0;
}

static int file_read(void *handle, void *out, uint32_t size, uint32_t *count)
{
    assert(handle == &disk_record && size == 4);
    memcpy(out, &disk_record, 4);
    *count = size;
    return 0;
}

static int file_write(void *handle, const void *record, uint32_t size, uint32_t *count)
{
    assert(handle == &disk_record && size == 4);
    writes++;
    *count = fail_write ? 0 : size;
    if (!fail_write) {
        memcpy(&disk_record, record, size);
        missing = 0;
    }
    return 0;
}

static void file_close(void *handle)
{
    assert(handle == &disk_record);
}

static void *settings_model(void *value)
{
    assert(value == &controller);
    return &model;
}

static uintptr_t on_off(int value)
{
    return value ? 2 : 1;
}

static void notify(void *value, uint32_t type, uint32_t property)
{
    assert(value == &model && type == OSOS_NOTIFY_ALL &&
           property == CFW_VolumeWarning_Value);
    notifications++;
}

#define osos_file_open file_open
#define osos_file_read file_read
#define osos_file_write file_write
#define osos_file_close file_close
#define osos_settings_model settings_model
#define osos_settings_on_off on_off
#define osos_model_notify notify
#include "../../../payload/volume_warning.c"

static uintptr_t value(void)
{
    uintptr_t out = 0;
    assert(cfw_volume_warning_property(OSOS_PROPERTY_STRING, CFW_VolumeWarning_Value,
                                       &out));
    return out;
}

int main(void)
{
    uintptr_t out;
    assert(!cfw_volume_warning_property(OSOS_PROPERTY_COUNT, CFW_VolumeWarning_Value,
                                        &out));
    assert(!cfw_volume_warning_action(&controller, "stock", 0));
    assert(cfw_volume_warning_action(&controller, "CFW_VolumeWarning_Toggle",
                                     OSOS_ACTION_SUPPORT_QUERY));
    assert(!writes && !notifications);

    /* A missing record keeps Apple's warning. */
    cfw_volume_warning_load();
    assert(value() == 2 && cfw_volume_warning_active());
    region = 0;
    assert(!cfw_volume_warning_active());
    region = 1;

    assert(cfw_volume_warning_action(&controller, "CFW_VolumeWarning_Toggle", 0));
    assert(disk_record == VOLUME_WARNING_OFF_RECORD && writes == 1 &&
           notifications == 1);
    assert(value() == 1 && !cfw_volume_warning_active());

    fail_write = 1;
    assert(cfw_volume_warning_action(&controller, "CFW_VolumeWarning_Toggle", 0));
    assert(value() == 1 && writes == 2 && notifications == 1);
    fail_write = 0;

    cfw_volume_warning_load();
    assert(value() == 1);
    assert(cfw_volume_warning_action(&controller, "CFW_VolumeWarning_Toggle", 0));
    assert(disk_record == VOLUME_WARNING_ON_RECORD && value() == 2);
    disk_record = 0;
    cfw_volume_warning_load();
    assert(value() == 2);
    return 0;
}
