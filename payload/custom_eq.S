/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"

.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

.global cfw_preferences_hook
.type cfw_preferences_hook,%function
cfw_preferences_hook:
    push {r4, lr}
    ldr r12, =0x0809c5e8
    blx r12
    bl cfw_eq_load_preferences
    pop {r4, pc}
.ltorg

/* Settings IDs 100..122 map to DSP presets 1..23. */

.global cfw_eq_map_hook
.type cfw_eq_map_hook,%function
cfw_eq_map_hook:
    sub r0, r0, #100
    cmp r0, #22
    addls r0, r0, #1
    movhi r0, #0
    bx lr

/* Preserve Flat's override behavior and apply it to Custom too. */

.global cfw_eq_track_guard
.type cfw_eq_track_guard,%function
cfw_eq_track_guard:
    cmp r0, #107
    cmpne r0, #122
    bx lr
