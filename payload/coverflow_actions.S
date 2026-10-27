/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"
#include "patches/sites.h"

.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

/* Replays the two instructions PATCH_JUMP replaced at OSOS_COVERFLOW_APPEAR. */
.global cfw_coverflow_appear_original
.type cfw_coverflow_appear_original,%function
cfw_coverflow_appear_original:
    push {r4, r5, r6, r7, r8, r9, lr}
    movs r5, r1
    ldr pc, =OSOS_COVERFLOW_APPEAR_RESUME
.ltorg
