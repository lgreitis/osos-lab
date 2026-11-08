/* SPDX-License-Identifier: GPL-3.0-only */
#include "osos.h"
.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

/* Each color hook forwards the view the native draw site keeps in a callee-saved
 * register, noted per hook. */
/* view in r6 */
.global cfw_theme_text_color_hook
.type cfw_theme_text_color_hook,%function
cfw_theme_text_color_hook:
    mov r2, r6
    b cfw_theme_text_color

/* view in r4 */
.global cfw_theme_marquee_color_hook
.type cfw_theme_marquee_color_hook,%function
cfw_theme_marquee_color_hook:
    mov r2, r4
    b cfw_theme_text_color

/* view in r4 */
.global cfw_theme_bitmap_color_hook
.type cfw_theme_bitmap_color_hook,%function
cfw_theme_bitmap_color_hook:
    mov r2, r4
    b cfw_theme_bitmap_color

/* view in r5 */
.global cfw_theme_search_key_color_hook
.type cfw_theme_search_key_color_hook,%function
cfw_theme_search_key_color_hook:
    mov r2, r5
    b cfw_theme_search_key_color

/* view in r4 */
.global cfw_theme_option_bar_color_hook
.type cfw_theme_option_bar_color_hook,%function
cfw_theme_option_bar_color_hook:
    mov r2, r4
    b cfw_theme_option_bar_color

/* Replaces the paletted blit calls of BitmapView_Draw and the list row icon
 * drawer (view in r4, bitmap context in r1). The callee takes alpha and mode on
 * the stack, so those two words are copied below the saved registers; r0 is
 * dead after every site. */
.global cfw_theme_bitmap_blit_hook
.type cfw_theme_bitmap_blit_hook,%function
cfw_theme_bitmap_blit_hook:
    push {r0, r1, r2, r3, lr}
    mov r0, r4
    bl cfw_theme_bitmap_blit_begin
.Lblit:
    pop {r0, r1, r2, r3, lr}
    push {r4, lr}
    ldr r4, [sp, #8]
    ldr ip, [sp, #12]
    push {r4, ip}
    ldr ip, =OSOS_BITMAP_BLIT
    blx ip
    add sp, sp, #8
    pop {r4, lr}
    b cfw_theme_bitmap_blit_end

/* Replaces the calendar cell drawer's alarm and event marker blits (cell in r4,
 * marker bitmap context in r1), with the same contract as above. */
.global cfw_theme_calendar_marker_blit_hook
.type cfw_theme_calendar_marker_blit_hook,%function
cfw_theme_calendar_marker_blit_hook:
    push {r0, r1, r2, r3, lr}
    mov r0, r4
    bl cfw_theme_calendar_marker_begin
    b .Lblit

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
