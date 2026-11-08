/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"

.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

/* r0 is the native preferences object + 4. */
.global cfw_preferences_hook
.type cfw_preferences_hook,%function
cfw_preferences_hook:
    push {r4, lr}
    mov r4, r0
    ldr r12, =OSOS_PREFERENCES_LOAD
    blx r12
    sub r0, r4, #4
    bl cfw_settings_boot
    pop {r4, pc}

/* Native save-all flushes its Preferences here; flush the store with it. */
.global cfw_preferences_flush_hook
.type cfw_preferences_flush_hook,%function
cfw_preferences_flush_hook:
    push {r4, lr}
    ldr r12, =OSOS_PREFERENCES_FLUSH
    blx r12
    bl cfw_store_flush
    pop {r4, pc}

/* Flush before the native sleep powers the disk down; preserve its argument. */
.global cfw_sleep_hook
.type cfw_sleep_hook,%function
cfw_sleep_hook:
    push {r0, lr}
    bl cfw_settings_sleep
    pop {r0, lr}
    ldr r12, =OSOS_SLEEP
    bx r12
.ltorg
