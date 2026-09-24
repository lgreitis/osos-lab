/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef APPLE_LAYOUT_H
#define APPLE_LAYOUT_H
#include <stdint.h>

#define OSOS_STAGE 0x0b000000u
#define COMPANION_STAGE 0x0bc00000u
#define BOOTSTRAP_BYTES 0x1f800u
#define OSOS_HEADER_BYTES 0x800u
#define OSOS_BODY_CAPACITY 0xb20000u
#define EXTENSION_BYTES 0xc000u
#define STAGED_HEADER (OSOS_STAGE + 0xb26000u)
#define BOOT_BODY_BYTES (*(volatile uint32_t *)0x0bb32000u)

#endif
