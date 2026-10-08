/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"

.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

.macro action_original name, resume
.global \name
.type \name,%function
\name:
    push {r4, r5, r6, r7, r8, lr}
    mov r7, r1
    ldr pc, =\resume
.endm

action_original cfw_song_action_original, OSOS_SONG_ACTION_RESUME
action_original cfw_playlist_action_original, OSOS_PLAYLIST_ACTION_RESUME
action_original cfw_genius_action_original, OSOS_GENIUS_ACTION_RESUME
.ltorg
