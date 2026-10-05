/* SPDX-License-Identifier: GPL-3.0-only */

.syntax unified
.cpu arm926ej-s
#include "common/recipe.inc"
#include <nor-target.h>
.thumb
.text
.balign 4
native_thumb_call apple_loader, APPLE_HOB_PATCH, reserve_hob, 0xff, 0xf7, 0x8b, 0xf9
.thumb_func
reserve_hob:
    /* Original HOB-init ABI: r0:r1 base, r2:r3 length. */
    push {r4, lr}
    movs r2, #3
    lsls r2, r2, #24
    movs r3, #0
    ldr r4, =APPLE_HOB_INIT
    blx r4
    pop {r4, pc}
.balign 4
native_thumb_call apple_loader, APPLE_DRAM_PATCH, reserve_dram, 0xfe, 0xf7, 0xc7, 0xff
.thumb_func
reserve_dram:
    /* Same ABI, including DRAM resource length passed into DXE prep. */
    push {r4, lr}
    movs r2, #3
    lsls r2, r2, #24
    movs r3, #0
    ldr r4, =APPLE_DRAM_INIT
    blx r4
    pop {r4, pc}
.balign 4
.ltorg
