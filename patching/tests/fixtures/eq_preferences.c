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

static unsigned char disk[128];
static uint32_t disk_size, writes, status, refreshes;
static int write_failure, filter_failure;
const struct cfw_ui_field cfw_eq_fields[CFW_EQ_FIELDS] = {
    {.count = 101}, {.count = 4},  {.count = 32}, {.count = 38}, {.count = 49},
    {.count = 4},   {.count = 32}, {.count = 38}, {.count = 49}, {.count = 4},
    {.count = 32},  {.count = 38}, {.count = 49},
};
const struct cfw_ui_action cfw_eq_save = {.count = 3};

static int osos_file_open(const struct osos_path *path, int mode, void **handle)
{
    assert(!strcmp(path->text, "iPod_Control\\Device\\CFWEQ"));
    assert(mode == 1 || mode == 2);
    *handle = disk;
    return 0;
}

static int osos_file_read(void *handle, void *out, uint32_t size, uint32_t *count)
{
    assert(handle == disk);
    *count = disk_size < size ? disk_size : size;
    memcpy(out, disk, *count);
    return 0;
}

static int osos_file_write(void *handle, const void *data, uint32_t size,
                           uint32_t *count)
{
    assert(handle == disk);
    writes++;
    if (write_failure)
        return -1;
    assert(size <= sizeof(disk));
    memcpy(disk, data, size);
    disk_size = size;
    *count = size;
    return 0;
}

static void osos_file_close(void *handle)
{
    assert(handle == disk);
}

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

#include "../../../payload/custom_eq.c"

static void saved(uint32_t version, uint32_t precut)
{
    struct eq_file file = {.magic = 0x51455743, .version = version};
    file.values[0] = precut;
    file.values[4] = 24;
    file.checksum = checksum(&file);
    memcpy(disk, &file, sizeof(file));
    disk_size = sizeof(file);
}

int main(void)
{
    saved(1, 7);
    cfw_eq_load_preferences();
    assert(settings[0] == 35 && settings[4] == 24 && !writes && !dirty);
    saved(1, 21);
    cfw_eq_load_preferences();
    assert(settings[0] == 0);
    saved(2, 37);
    cfw_eq_load_preferences();
    assert(settings[0] == 37);
    assert(cfw_eq_action("CFW_EQ_Save", OSOS_ACTION_SUPPORT_QUERY));
    assert(!writes);
    cfw_eq_model.change(0, 38);
    assert(settings[0] == 38 && dirty && refreshes == 1 && !writes);
    cfw_eq_model.change(CFW_EQ_FIELDS, 0);
    cfw_eq_model.change(0, 101);
    cfw_eq_model.change(0, 38);
    assert(refreshes == 1 && settings[0] == 38);
    write_failure = 1;
    cfw_eq_model.commit();
    assert(dirty && status == 1 && writes == 1);
    cfw_eq_action("CFW_EQ_Open", 0);
    assert(status == 1 && dirty && writes == 1);
    write_failure = 0;
    cfw_eq_action("CFW_EQ_Save", 0);
    assert(status == 0 && !dirty && writes == 2);
    struct eq_file file;
    memcpy(&file, disk, sizeof(file));
    assert(file.version == 2 && file.values[0] == 38 &&
           file.checksum == checksum(&file));
    cfw_eq_model.commit();
    assert(writes == 2);
    cfw_eq_load_preferences();
    assert(settings[0] == 38);
    filter_failure = 1;
    cfw_eq_model.change(0, 39);
    assert(settings[0] == 38 && status == 2 && !dirty);
    filter_failure = 0;
    cfw_eq_model.change(0, 39);
    cfw_eq_model.commit();
    assert(status == 0 && !dirty && writes == 3);
    cfw_eq_load_preferences();
    assert(settings[0] == 39);
    cfw_eq_model.change(0, 40);
    write_failure = 1;
    cfw_eq_model.commit();
    assert(status == 1 && dirty);
    write_failure = 0;
    cfw_eq_model.commit();
    assert(!dirty && status == 0 && writes == 5);
    return 0;
}
