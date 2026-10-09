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
    ldr r12, =OSOS_PREFERENCES_LOAD
    blx r12
    bl cfw_eq_load_preferences
    bl cfw_dark_mode_load
    pop {r4, pc}
.ltorg

