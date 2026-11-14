/* SPDX-License-Identifier: GPL-3.0-only */
.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

/* The native draw sites retain their view in r6, r4 and r5 respectively. */
.global cfw_dark_mode_text_color_hook
.type cfw_dark_mode_text_color_hook,%function
cfw_dark_mode_text_color_hook:
    mov r2, r6
    b cfw_dark_mode_text_color

.global cfw_dark_mode_marquee_color_hook
.type cfw_dark_mode_marquee_color_hook,%function
cfw_dark_mode_marquee_color_hook:
    mov r2, r4
    b cfw_dark_mode_text_color

.global cfw_dark_mode_background_fill_hook
.type cfw_dark_mode_background_fill_hook,%function
cfw_dark_mode_background_fill_hook:
    mov r2, r5
    b cfw_dark_mode_background_fill
