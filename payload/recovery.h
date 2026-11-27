/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_RECOVERY_H
#define CFW_RECOVERY_H
#include "panic.h"

/* The top of IRAM0 survives a warm SoC reset and stays above the bootstrap. */
#define CFW_RECOVERY_BYTES 512u
#define CFW_RECOVERY_ADDRESS (0x22020000u - CFW_RECOVERY_BYTES)
#define CFW_FIRMWARE_ID 0x660eb6ffu

/* written: the report was saved and read back once. */
struct cfw_crash_record {
    uint32_t commit, checksum, firmware, report_id, written;
    struct panic_frame frame;
    uint32_t backlight[4];
};

#define CFW_CRASH_RECORD ((volatile struct cfw_crash_record *)CFW_RECOVERY_ADDRESS)

void cfw_recovery_prepare(void);
void cfw_recovery_capture(const struct panic_frame *frame);
void cfw_recovery_backlight(const uint32_t result[4]);
int cfw_recovery_valid(void);
void cfw_recovery_path(char *out, uint32_t id);
void cfw_recovery_wait(void) __attribute__((noreturn));
#endif
