/* SPDX-License-Identifier: GPL-3.0-only */

#include "native/bindings.h"

.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

/* Replaces the call that starts output submission in the effects dispatcher.
 * There r6 is the processed PCM buffer, r4 its stereo frame count and r9 the
 * effects context; the original call's arguments and return value pass
 * through unchanged. */
.global cfw_sound_output_hook
.type cfw_sound_output_hook,%function
cfw_sound_output_hook:
    push {r0-r3, r12, lr}
    mov r0, r6
    mov r1, r4
    mov r2, r9
    bl cfw_sound_process
    pop {r0-r3, r12, lr}
    ldr pc, =OSOS_AUDIO_OUTPUT_BEGIN
.ltorg
