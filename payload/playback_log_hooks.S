/* SPDX-License-Identifier: GPL-3.0-only */
#include "patches/sites.h"
.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

.global cfw_playback_log_native_notify
.type cfw_playback_log_native_notify,%function
cfw_playback_log_native_notify:
    push {r3, r4, r5, lr}
    mov r5, r1
    ldr pc, =(OSOS_PLAYER_NOTIFY_ENTRY + 8)

.global cfw_playback_log_native_event
.type cfw_playback_log_native_event,%function
cfw_playback_log_native_event:
    push {r4, r5, r6, lr}
    mov r5, r0
    ldr pc, =(OSOS_PLAYER_EVENT_ENTRY + 8)

.global cfw_playback_log_native_complete
.type cfw_playback_log_native_complete,%function
cfw_playback_log_native_complete:
    push {r4, lr}
    mov r4, r0
    ldr pc, =(OSOS_TRACK_COMPLETE_ENTRY + 8)
