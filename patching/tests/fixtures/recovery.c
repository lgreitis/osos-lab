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
#include "../../../payload/store.h"

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
        assert(!f); /* Recovery never writes over an existing file. */
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

/* The store holds one record, the crash marks. */
static struct cfw_crash_mark stored_marks[CFW_CRASH_MARKS];
static uint32_t stored_bytes;
static unsigned mark_writes;

static uint32_t fake_store_get(uint32_t key, void *data, uint32_t capacity)
{
    assert(key == CFW_STORE_CRASHED);
    uint32_t size = stored_bytes < capacity ? stored_bytes : capacity;
    memcpy(data, stored_marks, size);
    return size;
}

static void fake_store_set(uint32_t key, const void *data, uint32_t size)
{
    assert(key == CFW_STORE_CRASHED && size <= sizeof stored_marks);
    memcpy(stored_marks, data, size);
    stored_bytes = size;
    mark_writes++;
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

#define cfw_store_get fake_store_get
#define cfw_store_set fake_store_set
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

static const struct cfw_crash_owner blamed = {
    0x1234abcd, 4096, 0x9999, "org.example.crashy", "Crashy Extension", "crashy.rvx"};
static const struct cfw_crash_owner other = {0x5555aaaa, 10, 0x77, "other", "Other", "o.rvx"};

static void capture_owner(const struct cfw_crash_owner *owner)
{
    struct panic_frame frame = {0};
    frame.type = 4;
    frame.dfar = 0xdeadbeef;
    frame.stack_count = 0xffffffff;
    cfw_recovery_prepare();
    assert(report_id);
    cfw_recovery_capture(&frame, owner);
    assert(cfw_recovery_valid() && retained.report_id == report_id);
    assert(cfw_recovery_owner() == (owner ? owner->hash : 0));
}

static unsigned marks(void)
{
    return stored_bytes / sizeof *stored_marks;
}

static void capture(void)
{
    capture_owner(NULL);
}

/* The record of a blamed extension outlives its report until the same mark
 * was found in the store at a later boot. */
static void quarantine(void)
{
    stored_bytes = 0;
    mark_writes = 0;
    capture_owner(&blamed);
    assert(!mark_writes);
    cfw_recovery_prepare();
    assert(mark_writes == 1 && marks() == 1);
    assert(stored_marks[0].hash == blamed.hash && stored_marks[0].size == 4096 &&
           stored_marks[0].content == 0x9999);
    assert(cfw_recovery_valid() && retained.written);
    assert(strstr(report_file()->data, "Attributed owner: 1234ABCD\n"));
    assert(strstr(report_file()->data, "Extension: Crashy Extension\n"));
    assert(strstr(report_file()->data, "Extension ID: org.example.crashy\n"));
    assert(strstr(report_file()->data, "File: crashy.rvx\n"));
    assert(strstr(report_file()->data, "File size: 00001000\n"));
    /* A lost flush: the mark is written again and the record stays. */
    stored_bytes = 0;
    cfw_recovery_prepare();
    assert(mark_writes == 2 && cfw_recovery_valid());
    cfw_recovery_prepare();
    assert(mark_writes == 2 && !cfw_recovery_valid());
    assert(marks() == 1);

    /* A mark of the same id for another file is replaced in place. */
    stored_marks[0].content = 0x1111;
    capture_owner(&blamed);
    cfw_recovery_prepare();
    assert(mark_writes == 3 && marks() == 1 && stored_marks[0].content == 0x9999);
    assert(cfw_recovery_valid());
    cfw_recovery_prepare();
    assert(!cfw_recovery_valid());

    /* A second extension is added after the first; the fifth drops the oldest. */
    capture_owner(&other);
    cfw_recovery_prepare();
    assert(marks() == 2 && stored_marks[0].hash == blamed.hash &&
           stored_marks[1].hash == other.hash);
    cfw_recovery_prepare();
    struct cfw_crash_owner third = other;
    for (uint32_t i = 1; i <= 3; i++) {
        third.hash = 0x100 + i;
        capture_owner(&third);
        cfw_recovery_prepare();
        cfw_recovery_prepare();
    }
    assert(marks() == 4 && stored_marks[0].hash == other.hash &&
           stored_marks[3].hash == 0x103);
    cfw_recovery_release(0x102);
    assert(marks() == 3 && stored_marks[2].hash == 0x103);
    stored_bytes = 0;

    /* The runtime releases a mark whose package is gone or changed. */
    capture_owner(&blamed);
    cfw_recovery_prepare();
    assert(marks() == 1 && retained.written);
    cfw_recovery_release(blamed.hash ^ 1);
    assert(marks() == 1 && cfw_recovery_valid());
    cfw_recovery_release(blamed.hash);
    assert(!stored_bytes && !cfw_recovery_valid());
    /* Before the report is saved, the release drops the mark only. */
    capture_owner(&blamed);
    fail_directory = 1;
    cfw_recovery_prepare(); /* marks, cannot write */
    assert(marks() == 1 && cfw_recovery_valid() && !retained.written);
    cfw_recovery_release(blamed.hash);
    assert(!stored_bytes && cfw_recovery_valid());
    fail_directory = 0;
    cfw_recovery_prepare();
    assert(marks() == 1 && retained.written);
    cfw_recovery_release(blamed.hash);
    assert(!cfw_recovery_valid());
    stored_bytes = 0;
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

    /* A saved report that was deleted or edited is not written again. */
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

    quarantine();

    /* The longest report fits the buffer. */
    retained.frame.stack_count = PANIC_STACK_WORDS;
    memset((void *)&retained.owner.id, 'x', sizeof retained.owner.id);
    memset((void *)&retained.owner.name, 'x', sizeof retained.owner.name);
    memset((void *)&retained.owner.file, 'x', sizeof retained.owner.file);
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
    puts("PASS: record integrity, report retries, id collisions, crash marks, Center packets");
    return 0;
}
