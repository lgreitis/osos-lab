/* SPDX-License-Identifier: GPL-3.0-only */

#include "patch.h"

/* Reserve the payload above Apple's BSS and stacks. */
PATCH_POINTER(0x0804B44C, 0x08B32B58, __payload_limit);
PATCH_POINTER(0x0807BFAC, 0x08B32B58, __payload_limit);
