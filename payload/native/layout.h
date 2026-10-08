/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/* Above native BSS; heap bounds are moved past this reservation. */
#define CFW_PAYLOAD_BASE 0x08b3c000
#define CFW_PAYLOAD_BYTES 0x000f0000
#define CFW_PAYLOAD_FILE_OFFSET 0x00a24500
