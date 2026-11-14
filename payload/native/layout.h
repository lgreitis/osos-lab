/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/* Above native BSS; bytes is the maximum, with the heap after the linked payload. */
#define CFW_PAYLOAD_BASE 0x08b3c000
#define CFW_PAYLOAD_BYTES 0x000f0000
#define CFW_PAYLOAD_FILE_OFFSET 0x00a24500
