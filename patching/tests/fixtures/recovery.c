/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* Native layout assertions assume 32-bit pointers; this fixture runs on the host. */
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert
#include "../../../payload/recovery.h"

static struct cfw_crash_record retained;
static uint32_t wheel[8], wheel_buttons;
static unsigned committed_syncs, writes;
static int fail_write, fail_read, fail_open, fail_directory, short_write;
static uint32_t partial_write;

struct file {
    char path[40], data[2048];
    uint32_t bytes, cursor;
};
static struct file files[128];
static unsigned file_count;

static struct file *find_file(const char *path)
{
    for (unsigned i = 0; i < file_count; i++)
        if (!strcmp(files[i].path, path))
            return &files[i];
    return NULL;
}

static int fake_open(const struct osos_path *path, uint32_t mode, void **handle)
{
    if (fail_open)
        return -99;
    struct file *f = find_file(path->text);
    if (mode == 1 && !f)
        return -43;
    if (mode == 2) {
        assert(!f); /* Recovery only creates files. */
        assert(file_count < sizeof files / sizeof *files);
        f = &files[file_count++];
        strcpy(f->path, path->text);
        writes++;
    }
    f->cursor = 0;
    *handle = f;
    return 0;
}

static int fake_read(void *handle, void *data, uint32_t bytes, uint32_t *read)
{
    struct file *f = handle;
    *read = 0;
    if (fail_read)
        return -99;
    if (f->cursor == f->bytes)
        return -39;
    if (bytes > f->bytes - f->cursor)
        bytes = f->bytes - f->cursor;
    memcpy(data, f->data + f->cursor, bytes);
    f->cursor += bytes;
    *read = bytes;
    return 0;
}

static int fake_write(void *handle, const void *data, uint32_t bytes, uint32_t *written)
{
    struct file *f = handle;
    assert(bytes < sizeof f->data);
    if ((fail_write || short_write) && partial_write < bytes)
        bytes = partial_write;
    memcpy(f->data, data, bytes);
    f->bytes = bytes;
    *written = bytes;
    return fail_write ? -1 : 0;
}

static void fake_close(void *handle)
{
    assert(handle);
}

static int fake_directory(const char *path, uint32_t volume)
{
    assert(!strcmp(path, "/Reprise/Crashes") && !volume);
    return fail_directory ? -1 : 0;
}

static uint32_t fake_clock(void)
{
    return 12345;
}

static char *fake_warm_boot(uint32_t mode)
{
    (void)mode;
    return NULL;
}

static void fake_reset(void)
{
    assert(0);
}

int cfw_recovery_valid(void);

void cfw_rvx_sync(const void *start, const void *end)
{
    (void)start;
    (void)end;
    if (retained.commit) {
        assert(cfw_recovery_valid());
        committed_syncs++;
    }
}

#define osos_file_open fake_open
#define osos_file_read fake_read
#define osos_file_write fake_write
#define osos_file_close fake_close
#undef CFW_CRASH_RECORD
#define CFW_CRASH_RECORD ((volatile struct cfw_crash_record *)&retained)
#undef OSOS_DIRECTORY_CREATE_WITH_PARENTS
#define OSOS_DIRECTORY_CREATE_WITH_PARENTS ((uintptr_t)fake_directory)
#undef OSOS_CLOCK_GET_MICROSECONDS
#define OSOS_CLOCK_GET_MICROSECONDS ((uintptr_t)fake_clock)
#undef OSOS_SET_WARM_BOOT_MODE_TAG
#define OSOS_SET_WARM_BOOT_MODE_TAG ((uintptr_t)fake_warm_boot)
#undef OSOS_RESET_SOC_AND_WAIT
#define OSOS_RESET_SOC_AND_WAIT ((uintptr_t)fake_reset)
#undef OSOS_WHEEL_STATE_BUTTONS
#define OSOS_WHEEL_STATE_BUTTONS ((uintptr_t)&wheel_buttons)
#undef OSOS_WHEEL_CONTROLLER
#define OSOS_WHEEL_CONTROLLER ((uintptr_t)wheel)
#include "../../../payload/recovery.c"

static struct file *report_file(void)
{
    char path[32];
    cfw_recovery_path(path, retained.report_id);
    return find_file(path);
}

static void capture(void)
{
    struct panic_frame frame = {0};
    frame.type = 4;
    frame.dfar = 0xdeadbeef;
    frame.stack_count = 0xffffffff;
    cfw_recovery_prepare();
    assert(report_id);
    cfw_recovery_capture(&frame);
    assert(cfw_recovery_valid() && retained.report_id == report_id);
}

/* A short write is kept and the report goes to a fresh id at the next boot. */
static void retry_failed_write(uint32_t partial)
{
    capture();
    fail_write = 1;
    partial_write = partial;
    cfw_recovery_prepare();
    assert(cfw_recovery_valid());
    struct file *old = report_file();
    assert(old && old->bytes == partial);
    uint32_t id = retained.report_id;
    unsigned before = writes;
    char saved[sizeof old->data];
    memcpy(saved, old->data, sizeof saved);
    assert(!retained.written);
    fail_write = 0;
    cfw_recovery_prepare();
    assert(writes == before + 1 && retained.report_id != id && cfw_recovery_valid());
    assert(!memcmp(saved, old->data, sizeof saved) && old->bytes == partial);
    cfw_recovery_prepare();
    assert(!cfw_recovery_valid());
}

int main(void)
{
    capture();
    uint32_t backlight[4] = {1, 0x10, 0x90, 0x184};
    cfw_recovery_backlight(backlight);
    assert(cfw_recovery_valid());
    assert(retained.backlight[0] == 1 && retained.backlight[3] == 0x184);
    uint32_t commit = retained.commit;
    retained.commit = 0;
    assert(!cfw_recovery_valid());
    retained.commit = commit;
    retained.frame.dfar ^= 1;
    assert(!cfw_recovery_valid());
    retained.frame.dfar ^= 1;
    retained.firmware ^= 1;
    assert(!cfw_recovery_valid());
    retained.firmware ^= 1;

    /* Written at the next boot, kept until a later boot reads it back. */
    cfw_recovery_prepare();
    assert(writes == 1 && cfw_recovery_valid() && retained.written);
    assert(!strncmp(report_file()->data, "RepriseOS crash report v1\n", 26));
    assert(strstr(report_file()->data, "DFAR: DEADBEEF\n"));
    assert(strstr(report_file()->data, "Backlight (1=on): 00000001\n"));
    assert(report_id != retained.report_id);
    uint32_t id = retained.report_id;
    fail_read = 1;
    cfw_recovery_prepare();
    assert(writes == 1 && cfw_recovery_valid() && retained.report_id == id);
    fail_read = 0;
    fail_open = 1;
    cfw_recovery_prepare();
    assert(writes == 1 && cfw_recovery_valid() && !report_id);
    fail_open = 0;
    cfw_recovery_prepare();
    assert(writes == 1 && !cfw_recovery_valid());

    /* A foreign file under the id moves the report past a taken id. */
    capture();
    id = retained.report_id;
    char path[32];
    void *handle;
    struct osos_path native;
    cfw_recovery_path(path, id);
    osos_path_init(&native, path);
    assert(!fake_open(&native, 2, &handle));
    struct file *old = handle;
    strcpy(old->data, "foreign");
    old->bytes = 7;
    uint32_t collision = unused_id(id);
    cfw_recovery_path(path, collision);
    osos_path_init(&native, path);
    assert(!fake_open(&native, 2, &handle));
    unsigned before = writes;
    cfw_recovery_prepare();
    assert(writes == before + 1 && cfw_recovery_valid() && retained.written);
    assert(retained.report_id != id && retained.report_id != collision);
    assert(!strcmp(old->data, "foreign"));
    cfw_recovery_prepare();
    assert(!cfw_recovery_valid());

    /* A deleted or edited saved report clears the record without a write. */
    capture();
    cfw_recovery_prepare();
    before = writes;
    report_file()->path[0] = 0;
    cfw_recovery_prepare();
    assert(writes == before && !cfw_recovery_valid());
    capture();
    cfw_recovery_prepare();
    before = writes;
    report_file()->data[0] ^= 1;
    cfw_recovery_prepare();
    assert(writes == before && !cfw_recovery_valid());
    retry_failed_write(0);
    retry_failed_write(64);
    retry_failed_write(73);

    /* A short write the file system reports as complete fails the read-back. */
    capture();
    short_write = 1;
    partial_write = 64;
    cfw_recovery_prepare();
    assert(cfw_recovery_valid() && !retained.written && report_file()->bytes == 64);
    short_write = 0;
    cfw_recovery_prepare();
    assert(cfw_recovery_valid() && retained.written);
    cfw_recovery_prepare();
    assert(!cfw_recovery_valid());

    capture();
    id = retained.report_id;
    before = writes;
    fail_directory = 1;
    cfw_recovery_prepare();
    assert(writes == before && retained.report_id == id && cfw_recovery_valid());
    fail_directory = 0;
    cfw_recovery_prepare();
    assert(writes == before + 1);

    /* The longest report fits the buffer. */
    retained.frame.stack_count = PANIC_STACK_WORDS;
    char formatted[REPORT_BYTES + 1];
    assert(format(formatted) < REPORT_BYTES);

    unsigned released = 0;
    assert(!center_pressed(&released, WHEEL_PACKET_VALUE | (WHEEL_CENTER << 8)));
    assert(!center_pressed(&released, 0) && !released);
    assert(!center_pressed(&released, WHEEL_PACKET_VALUE) && released);
    assert(!center_pressed(&released, WHEEL_PACKET_VALUE | (3 << 8)));
    assert(center_pressed(&released, WHEEL_PACKET_VALUE | (WHEEL_CENTER << 8)));
    released = 0;
    assert(!center_pressed(&released, WHEEL_BUTTONS_VALUE));
    assert(center_pressed(&released, WHEEL_BUTTONS_VALUE | (WHEEL_CENTER << 16)));
    assert(committed_syncs >= 10);
    puts("PASS: record integrity, report retries, id collisions, Center packets");
    return 0;
}
