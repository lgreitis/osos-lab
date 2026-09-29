/* SPDX-License-Identifier: GPL-3.0-only */

#include "patch.h"

/* Report the stock non-European volume policy without changing SysInfo. */
PATCH_WORD(0x0804b15c, 0xe2000001, 0xe3a00000);
