/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_RECOVERY_H
#define CFW_RECOVERY_H
#include "panic.h"

/* The top of IRAM0 survives a warm SoC reset and stays above the bootstrap. */
#define CFW_RECOVERY_BYTES 512u
#define CFW_RECOVERY_ADDRESS (0x22020000u - CFW_RECOVERY_BYTES)
#define CFW_FIRMWARE_ID 0x660eb6ffu

/* The extension a crash is attributed to: the hash of its id, its file's size
 * and body hash, and its texts. The runtime fills it; hash 0 blames the core. */
struct cfw_crash_owner {
    uint32_t hash, size, content;
    char id[33], name[64], file[36];
};

/* written: the report was saved and read back once. */
struct cfw_crash_record {
    uint32_t commit, checksum, firmware, report_id, written;
    struct cfw_crash_owner owner;
    struct panic_frame frame;
    uint32_t backlight[4];
};

#define CFW_CRASH_RECORD ((volatile struct cfw_crash_record *)CFW_RECOVERY_ADDRESS)

void cfw_recovery_prepare(void);
void cfw_recovery_capture(const struct panic_frame *frame,
                          const struct cfw_crash_owner *owner);
/* The id hash the valid record blames, 0 for the core or without a record. */
uint32_t cfw_recovery_owner(void);
/* Drops the crash mark naming `hash` and the record it came from once its report
 * is saved. The runtime calls it when the marked package is missing or changed. */
void cfw_recovery_release(uint32_t hash);
void cfw_recovery_backlight(const uint32_t result[4]);
int cfw_recovery_valid(void);
void cfw_recovery_path(char *out, uint32_t id);
void cfw_recovery_wait(void) __attribute__((noreturn));
#endif
