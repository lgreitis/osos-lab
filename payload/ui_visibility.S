/* SPDX-License-Identifier: GPL-3.0-only */
#include "osos.h"
.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4
.global cfw_main_menu_reset_original
.type cfw_main_menu_reset_original,%function
cfw_main_menu_reset_original:
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r9, r0
    ldr pc, =OSOS_MAIN_MENU_RESET_RESUME

.global cfw_music_menu_reset_original
.type cfw_music_menu_reset_original,%function
cfw_music_menu_reset_original:
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r8, r0
    ldr pc, =OSOS_MUSIC_MENU_RESET_RESUME

.ltorg
