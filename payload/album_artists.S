/* SPDX-License-Identifier: GPL-3.0-only */
#include "osos.h"
.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

.global cfw_artists_build_original
.type cfw_artists_build_original,%function
cfw_artists_build_original:
    push {r3, r4, r5, lr}
    ldr r3, =OSOS_BROWSER_ARTIST_GROUP
    ldr pc, =OSOS_ARTISTS_BUILD_RESUME

.global cfw_artist_name_original
.type cfw_artist_name_original,%function
cfw_artist_name_original:
    push {r4, r5, r6, r7, r8, lr}
    .word OSOS_ARTIST_NAME_SAVE_WORD
    ldr pc, =OSOS_ARTIST_NAME_RESUME

.global cfw_artist_letter_original
.type cfw_artist_letter_original,%function
cfw_artist_letter_original:
    push {r4, r5, r6, r7, r8, lr}
    mov r7, r0
    ldr pc, =OSOS_ARTIST_LETTER_RESUME

.global cfw_browser_destroy_original
.type cfw_browser_destroy_original,%function
cfw_browser_destroy_original:
    push {r4, lr}
    mov r4, r0
    ldr pc, =OSOS_MODEL_DESTROY_RESUME

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

.global cfw_album_artist_filter_hook
.type cfw_album_artist_filter_hook,%function
cfw_album_artist_filter_hook:
    /* Native filter keeps browser in r4 and entry in r5; fifth compare arg is on SP. */
    push {r0, r1, r2, r3, r12, lr}
    mov r0, r4
    mov r1, r5
    bl cfw_album_artist_compare_filter
    cmp r0, #2
    strne r0, [sp]
    popne {r0, r1, r2, r3, r12, pc}
    pop {r0, r1, r2, r3, r12, lr}
    ldr pc, =OSOS_TEXT_COMPARE
.ltorg
