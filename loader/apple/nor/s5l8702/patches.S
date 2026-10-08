/* SPDX-License-Identifier: GPL-3.0-only */

.syntax unified
.cpu arm926ej-s
#include "common/recipe.inc"
#include "nor/bindings.h"

native_thumb_call apple_loader, APPLE_DXE_CALL_PATCH, before_dxe, 0x01, 0xf0, 0x3c, 0xe8

/* Keep the original MMU setup and verify the padding used by our payload. */
native_expect apple_loader, APPLE_MMU_CHECK, 0xfd, 0xf7, 0xca, 0xff
#include <loader-checks.inc>
native_expect_fill apple_loader, 0x1a730, (0x1f510 - 0x1a730), 0xff
native_ffs_checksum apple_loader, APPLE_PREEFI_FFS
