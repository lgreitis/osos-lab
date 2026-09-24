/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef REPRISE_UPLOAD_H
#define REPRISE_UPLOAD_H

#include <stdint.h>

#define UPLOAD_CHUNK 65536u
#define UPLOAD_DATA ((uint8_t *)0x09800000u)
#define UPLOAD_RESULT ((struct upload_result *)0x2201fa80u)
#define UPLOAD_LIMIT 0x7fffffffu

struct upload_config {
    uint8_t tag[16];
    uint32_t size, overwrite;
    uint8_t sha256[32];
    char path[192];
};

struct upload_result {
    uint32_t magic, version;
    uint8_t nonce[16];
    uint32_t state;
    int32_t rc;
    uint32_t size, received, written, verified, endpoint, high_speed, error_no,
        rejected;
    uint8_t source_sha256[32], disk_sha256[32];
    char path[192];
};

_Static_assert(sizeof(struct upload_config) == 248, "upload config");
_Static_assert(sizeof(struct upload_result) == 320, "upload result");

extern const volatile struct upload_config upload_config;

#define STORAGE_RESULT ((struct storage_result *)0x2201fbc0u)


struct storage_result {
    uint64_t start, end, free_bytes;
    uint32_t sector_bytes, existing_files;
};

void upload_prepare(uint64_t start, uint64_t end, unsigned bytes);
void upload_start(void);
void upload_write(unsigned bytes);
void upload_verify_step(void);
void upload_fail(int rc);
void upload_close(void);
int upload_usb(void);

#endif
