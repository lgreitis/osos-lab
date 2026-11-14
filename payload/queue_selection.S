/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"

.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

/* Replay the two instructions replaced at the generic media-list dispatcher entry. */
.global cfw_media_list_action_original
.type cfw_media_list_action_original,%function
cfw_media_list_action_original:
    push {r4, r5, r6, r7, r8, lr}
    mov r7, r1
    ldr pc, =OSOS_MEDIA_LIST_ACTION_RESUME
.ltorg
