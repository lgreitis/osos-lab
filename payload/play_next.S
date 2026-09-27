/* SPDX-License-Identifier: GPL-3.0-only */

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

action_original cfw_song_action_original, 0x082176c8
action_original cfw_playlist_action_original, 0x0822d1c0
action_original cfw_genius_action_original, 0x0821ac88
.ltorg
