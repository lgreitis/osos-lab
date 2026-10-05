/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/* Above native BSS and stack tops; both heap bounds are patched to the limit. */
#define CFW_PAYLOAD_BASE 0x08b22000
#define CFW_PAYLOAD_BYTES 0x100000
#define CFW_PAYLOAD_FILE_OFFSET 0x00a06e40
