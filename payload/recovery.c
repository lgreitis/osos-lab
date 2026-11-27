/* SPDX-License-Identifier: GPL-3.0-only */
#include "recovery.h"
#include "osos.h"
#include "store.h"
#include <stddef.h>

#define REG32(address) (*(volatile uint32_t *)(address))
#define WHEEL_STATUS REG32(OSOS_WHEEL_CONTROLLER + 0x0c)
#define WHEEL_ACKNOWLEDGE REG32(OSOS_WHEEL_CONTROLLER + 0x14)
#define WHEEL_PACKET REG32(OSOS_WHEEL_CONTROLLER + 0x18)
#define WHEEL_READY 0x1u
#define WHEEL_ACK 0x7u
#define WHEEL_PACKET_MASK 0xbc0000ffu
#define WHEEL_PACKET_VALUE 0x8000001au
#define WHEEL_BUTTONS_MASK 0x8000ffffu
#define WHEEL_BUTTONS_VALUE 0x8000023au
#define WHEEL_CENTER 0x1u
#define WARM_BOOT_NONE 0u
#define COMMIT 0x31524352u /* RCR1 */
#define REPORT_BYTES 1024u

enum { FILE_NOT_FOUND = -43, FILE_EOF = -39 };

_Static_assert(sizeof(struct cfw_crash_record) <= CFW_RECOVERY_BYTES,
               "Crash reservation");

extern void cfw_rvx_sync(const void *start, const void *end);

static uint32_t report_id;
static const char hex_digits[] = "0123456789ABCDEF";

static uint32_t checksum(void)
{
    const volatile uint8_t *p = (const volatile uint8_t *)CFW_CRASH_RECORD;
    uint32_t hash = 2166136261u;
    for (uint32_t i = 8; i < sizeof *CFW_CRASH_RECORD; i++)
        hash = (hash ^ p[i]) * 16777619u;
    return hash;
}

static void sync_record(void)
{
    cfw_rvx_sync((const void *)CFW_CRASH_RECORD, (const void *)(CFW_CRASH_RECORD + 1));
}

static void clear(void)
{
    CFW_CRASH_RECORD->commit = 0;
    sync_record();
}

/* The checksum is written and flushed before the commit word, so a reset in
 * between leaves an invalid record rather than a torn one. */
static void commit(void)
{
    CFW_CRASH_RECORD->checksum = checksum();
    sync_record();
    CFW_CRASH_RECORD->commit = COMMIT;
    sync_record();
}

int cfw_recovery_valid(void)
{
    return CFW_CRASH_RECORD->commit == COMMIT &&
           CFW_CRASH_RECORD->firmware == CFW_FIRMWARE_ID &&
           CFW_CRASH_RECORD->checksum == checksum();
}

uint32_t cfw_recovery_owner(void)
{
    return cfw_recovery_valid() ? CFW_CRASH_RECORD->owner.hash : 0;
}

void cfw_recovery_capture(const struct panic_frame *frame,
                          const struct cfw_crash_owner *owner)
{
    clear();
    volatile uint8_t *p = (volatile uint8_t *)CFW_CRASH_RECORD;
    for (uint32_t i = 4; i < sizeof *CFW_CRASH_RECORD; i++)
        p[i] = 0;
    CFW_CRASH_RECORD->firmware = CFW_FIRMWARE_ID;
    CFW_CRASH_RECORD->report_id = report_id;
    if (owner) {
        const uint8_t *from = (const uint8_t *)owner;
        volatile uint8_t *to = (volatile uint8_t *)&CFW_CRASH_RECORD->owner;
        for (uint32_t i = 0; i < sizeof *owner; i++)
            to[i] = from[i];
    }
    const uint8_t *from = (const uint8_t *)frame;
    volatile uint8_t *to = (volatile uint8_t *)&CFW_CRASH_RECORD->frame;
    for (uint32_t i = 0; i < sizeof *frame; i++)
        to[i] = from[i];
    commit();
}

void cfw_recovery_backlight(const uint32_t result[4])
{
    if (!cfw_recovery_valid())
        return;
    clear();
    for (unsigned i = 0; i < 4; i++)
        CFW_CRASH_RECORD->backlight[i] = result[i];
    commit();
}

void cfw_recovery_path(char *out, uint32_t id)
{
    const char *prefix = "/Reprise/Crashes/";
    while (*prefix)
        *out++ = *prefix++;
    for (int shift = 28; shift >= 0; shift -= 4)
        *out++ = hex_digits[id >> shift & 15];
    const char *suffix = ".txt";
    while ((*out++ = *suffix++)) {
    }
}

static char *append(char *out, const char *value)
{
    while (*value)
        *out++ = *value++;
    return out;
}

static char *field(char *out, const char *key, const volatile char *value,
                   uint32_t capacity)
{
    out = append(out, key);
    out = append(out, ": ");
    for (uint32_t i = 0; i < capacity && value[i]; i++)
        *out++ = value[i];
    *out++ = '\n';
    return out;
}

static char *word(char *out, const char *key, uint32_t value)
{
    out = append(out, key);
    out = append(out, ": ");
    for (int shift = 28; shift >= 0; shift -= 4)
        *out++ = hex_digits[value >> shift & 15];
    *out++ = '\n';
    return out;
}

static uint32_t format(char *buffer)
{
    char *p = append(buffer, "RepriseOS crash report v1\n");
    p = word(p, "Report", CFW_CRASH_RECORD->report_id);
    p = word(p, "Firmware", CFW_CRASH_RECORD->firmware);
    const volatile struct cfw_crash_owner *owner = &CFW_CRASH_RECORD->owner;
    p = word(p, "Attributed owner", owner->hash);
    p = field(p, "Extension", owner->name, sizeof owner->name);
    p = field(p, "Extension ID", owner->id, sizeof owner->id);
    p = field(p, "File", owner->file, sizeof owner->file);
    p = word(p, "File size", owner->size);
    static const char *const labels[] = {
        "R0",   "R1",           "R2",          "R3",   "R4",         "R5",
        "R6",   "R7",           "R8",          "R9",   "R10",        "R11",
        "R12",  "Exception LR", "Type",        "CPSR", "SPSR",       "DFSR",
        "DFAR", "IFSR",         "Frame valid", "SP",   "Stack words"};
    const volatile uint32_t *w = (const volatile uint32_t *)&CFW_CRASH_RECORD->frame;
    for (unsigned i = 0; i < sizeof labels / sizeof *labels; i++)
        p = word(p, labels[i], w[i]);
    for (unsigned i = 0;
         i < PANIC_STACK_WORDS && i < CFW_CRASH_RECORD->frame.stack_count; i++)
        p = word(p, "Stack", CFW_CRASH_RECORD->frame.stack[i]);
    p = word(p, "Backlight (1=on)", CFW_CRASH_RECORD->backlight[0]);
    p = word(p, "I2C clock gates", CFW_CRASH_RECORD->backlight[1]);
    p = word(p, "I2C status", CFW_CRASH_RECORD->backlight[2]);
    p = word(p, "I2C control", CFW_CRASH_RECORD->backlight[3]);
    p = append(p, "Issue: https://github.com/RepriseOS/repriseos/issues\n");
    return (uint32_t)(p - buffer);
}

enum report_match { REPORT_MISSING, REPORT_SAME, REPORT_DIFFERENT, REPORT_UNREADABLE };

static int open_path(const char *text, uint32_t mode, void **handle)
{
    struct osos_path path;
    osos_path_init(&path, text);
    return osos_file_open(&path, mode, handle);
}

/* Only the captured bytes matter; a trailing suffix does not invalidate them. */
static enum report_match matches(const char *path, const char *expected, uint32_t bytes)
{
    void *handle;
    int status = open_path(path, 1, &handle);
    if (status)
        return status == FILE_NOT_FOUND ? REPORT_MISSING : REPORT_UNREADABLE;
    char chunk[64];
    enum report_match result = REPORT_SAME;
    for (uint32_t at = 0; at < bytes && result == REPORT_SAME;) {
        uint32_t want = bytes - at, got = 0;
        if (want > sizeof chunk)
            want = sizeof chunk;
        status = osos_file_read(handle, chunk, want, &got);
        if (status && status != FILE_EOF) {
            result = REPORT_UNREADABLE;
            break;
        }
        if (got != want)
            result = REPORT_DIFFERENT;
        for (uint32_t i = 0; result == REPORT_SAME && i < got; i++)
            if (chunk[i] != expected[at + i])
                result = REPORT_DIFFERENT;
        at += want;
    }
    osos_file_close(handle);
    return result;
}

/* Report ids are non-security PRNG values. A failed open only proves absence
 * when the file system reports the file as not found. */
static uint32_t unused_id(uint32_t seed)
{
    char path[32];
    uint32_t id = seed ? seed : 1;
    for (unsigned attempt = 0; attempt < 64; attempt++) {
        id ^= id << 13;
        id ^= id >> 17;
        id ^= id << 5;
        if (cfw_recovery_valid() && id == CFW_CRASH_RECORD->report_id)
            continue;
        cfw_recovery_path(path, id);
        void *handle;
        int status = open_path(path, 1, &handle);
        if (!status) {
            osos_file_close(handle);
            continue;
        }
        return status == FILE_NOT_FOUND ? id : 0;
    }
    return 0;
}

static unsigned read_marks(struct cfw_crash_mark *marks)
{
    uint32_t bytes = cfw_store_get(CFW_STORE_CRASHED, marks,
                                   CFW_CRASH_MARKS * sizeof *marks);
    return bytes / sizeof *marks;
}

static void write_marks(const struct cfw_crash_mark *marks, unsigned count)
{
    cfw_store_set(CFW_STORE_CRASHED, marks, count * sizeof *marks);
}

/* Marks the blamed extension in the store. The record stays until a boot finds
 * the same mark already there: cfw_settings_boot() flushes the store after
 * this, so the mark reaches the disk before the record can go. */
static int mark_owner(void)
{
    const volatile struct cfw_crash_owner *owner = &CFW_CRASH_RECORD->owner;
    if (!owner->hash)
        return 1;
    struct cfw_crash_mark marks[CFW_CRASH_MARKS];
    unsigned count = read_marks(marks), at = 0;
    while (at < count && marks[at].hash != owner->hash)
        at++;
    if (at < count && marks[at].size == owner->size && marks[at].content == owner->content)
        return 1;
    if (at == count && count == CFW_CRASH_MARKS) {
        /* Full: the oldest mark makes room. */
        for (unsigned i = 1; i < count; i++)
            marks[i - 1] = marks[i];
        at = count - 1;
    } else if (at == count) {
        count++;
    }
    marks[at].hash = owner->hash;
    marks[at].size = owner->size;
    marks[at].content = owner->content;
    write_marks(marks, count);
    return 0;
}

void cfw_recovery_release(uint32_t hash)
{
    if (!hash)
        return;
    struct cfw_crash_mark marks[CFW_CRASH_MARKS];
    unsigned count = read_marks(marks), kept = 0;
    for (unsigned i = 0; i < count; i++)
        if (marks[i].hash != hash)
            marks[kept++] = marks[i];
    if (kept != count)
        write_marks(marks, kept);
    if (cfw_recovery_owner() == hash && CFW_CRASH_RECORD->written)
        clear();
}

/* Writes the report of the last crash and picks the id the next crash will be
 * saved as. The record stays until a later boot reads the report back unchanged,
 * or finds a report it already saved deleted or edited, and the mark of the
 * blamed extension has survived a store flush. */
void cfw_recovery_prepare(void)
{
    typedef int (*directory_fn)(const char *, uint32_t);
    typedef uint32_t (*clock_fn)(void);
    int directory =
        !((directory_fn)OSOS_DIRECTORY_CREATE_WITH_PARENTS)("/Reprise/Crashes", 0);
    if (cfw_recovery_valid() && CFW_CRASH_RECORD->report_id) {
        int marked = mark_owner();
        char path[32], report[REPORT_BYTES];
        uint32_t bytes = format(report);
        cfw_recovery_path(path, CFW_CRASH_RECORD->report_id);
        enum report_match existing = matches(path, report, bytes);
        if (existing == REPORT_SAME ||
            (CFW_CRASH_RECORD->written && existing != REPORT_UNREADABLE)) {
            if (marked)
                clear();
        } else if (directory && existing != REPORT_UNREADABLE) {
            if (existing == REPORT_DIFFERENT) {
                uint32_t id = unused_id(CFW_CRASH_RECORD->report_id);
                if (!id)
                    goto next_report;
                /* Commit the new destination before writing. A reset can leave
                 * a partial file, but the next boot must never overwrite it. */
                clear();
                CFW_CRASH_RECORD->report_id = id;
                commit();
                cfw_recovery_path(path, id);
                bytes = format(report);
            }
            void *handle;
            if (!open_path(path, 2, &handle)) {
                uint32_t written = 0;
                int status = osos_file_write(handle, report, bytes, &written);
                osos_file_close(handle);
                if (!status && matches(path, report, bytes) == REPORT_SAME) {
                    clear();
                    CFW_CRASH_RECORD->written = 1;
                    commit();
                }
            }
        }
    }
next_report:
    report_id = unused_id(((clock_fn)OSOS_CLOCK_GET_MICROSECONDS)());
}

static int center_pressed(unsigned *released, uint32_t packet)
{
    uint32_t buttons;
    if ((packet & WHEEL_PACKET_MASK) == WHEEL_PACKET_VALUE)
        buttons = packet >> 8 & 31;
    else if ((packet & WHEEL_BUTTONS_MASK) == WHEEL_BUTTONS_VALUE)
        buttons = packet >> 16 & 31;
    else
        return 0;
    if (!buttons)
        *released = 1;
    return *released && buttons == WHEEL_CENTER;
}

/* Polls the wheel controller with interrupts masked; Center, pressed after every
 * button was let go, restarts. */
void cfw_recovery_wait(void)
{
    unsigned released = !*(volatile uint32_t *)OSOS_WHEEL_STATE_BUTTONS;
    /* A packet already waiting at the crash cannot request a restart. */
    if (WHEEL_STATUS & WHEEL_READY) {
        uint32_t pending = WHEEL_PACKET;
        WHEEL_ACKNOWLEDGE = WHEEL_ACK;
        released = 0;
        center_pressed(&released, pending);
    }
    for (;;) {
        if (!(WHEEL_STATUS & WHEEL_READY))
            continue;
        uint32_t value = WHEEL_PACKET;
        WHEEL_ACKNOWLEDGE = WHEEL_ACK;
        if (center_pressed(&released, value)) {
            ((char *(*)(uint32_t))OSOS_SET_WARM_BOOT_MODE_TAG)(WARM_BOOT_NONE);
            ((void (*)(void))OSOS_RESET_SOC_AND_WAIT)();
        }
    }
}
