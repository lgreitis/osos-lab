/* SPDX-License-Identifier: GPL-3.0-only */
#include "osos.h"
.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

/* The native draw sites retain their view in r6, r4 and r5 respectively. */
.global cfw_theme_text_color_hook
.type cfw_theme_text_color_hook,%function
cfw_theme_text_color_hook:
    mov r2, r6
    b cfw_theme_text_color

.global cfw_theme_marquee_color_hook
.type cfw_theme_marquee_color_hook,%function
cfw_theme_marquee_color_hook:
    mov r2, r4
    b cfw_theme_text_color

.global cfw_theme_background_fill_hook
.type cfw_theme_background_fill_hook,%function
cfw_theme_background_fill_hook:
    mov r2, r5
    b cfw_theme_background_fill

/* Replaces the selection bar's branch on the native style flag (Z set: RAM colors).
 * The drawer keeps no live value in r0-r3, ip or lr at this point. */
.global cfw_theme_selection_bar_hook
.type cfw_theme_selection_bar_hook,%function
cfw_theme_selection_bar_hook:
    moveq r0, #1
    movne r0, #0
    push {r0, lr}
    bl cfw_theme_selection_bar_prepare
    pop {r1, lr}
    cmp r0, #0
    ldrne pc, =OSOS_SELECTION_RAM_GRADIENT
    bx lr
.ltorg
