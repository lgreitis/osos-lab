/* SPDX-License-Identifier: GPL-3.0-only */

.syntax unified
.cpu arm926ej-s
#include "common/recipe.inc"
.thumb
.text
.balign 4
native_thumb_call apple_loader, 0x10596, reserve_hob, 0xff, 0xf7, 0x8b, 0xf9
.thumb_func
reserve_hob:
    /* Original HOB-init ABI: r0:r1 base, r2:r3 length. */
    push {r4, lr}
    movs r2, #3
    lsls r2, r2, #24
    movs r3, #0
    ldr r4, =0x2200f8b1
    blx r4
    pop {r4, pc}
.balign 4
native_thumb_call apple_loader, 0x105be, reserve_dram, 0xfe, 0xf7, 0xc7, 0xff
.thumb_func
reserve_dram:
    /* Same ABI, including DRAM resource length passed into DXE prep. */
    push {r4, lr}
    movs r2, #3
    lsls r2, r2, #24
    movs r3, #0
    ldr r4, =0x2200f551
    blx r4
    pop {r4, pc}
.balign 4
.ltorg
