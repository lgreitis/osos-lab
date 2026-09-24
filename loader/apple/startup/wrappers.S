/* SPDX-License-Identifier: GPL-3.0-only */

/* Loaded at PreEfi + 0x40 (0x2200e23c), in DOS-stub padding.
 * PreEfi remains at its original address.
 */
.syntax unified
.cpu arm926ej-s
.thumb
.text
.balign 4
/* Entry +0: original HOB-init ABI: r0:r1 base, r2:r3 length. */
    push {r4, lr}
    movs r2, #3
    lsls r2, r2, #24
    movs r3, #0
    ldr r4, =0x2200f8b1
    blx r4
    pop {r4, pc}
.balign 4
/* Entry +16: same ABI, including DRAM resource length passed into DXE prep. */
    push {r4, lr}
    movs r2, #3
    lsls r2, r2, #24
    movs r3, #0
    ldr r4, =0x2200f551
    blx r4
    pop {r4, pc}
.balign 4
.ltorg
