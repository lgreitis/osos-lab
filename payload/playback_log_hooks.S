/* SPDX-License-Identifier: GPL-3.0-only */
.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

.global cfw_playback_log_native_complete
.type cfw_playback_log_native_complete,%function
cfw_playback_log_native_complete:
    push {r4, lr}
    mov r4, r0
    ldr pc, =0x0817392c
