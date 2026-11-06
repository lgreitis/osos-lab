/* SPDX-License-Identifier: GPL-3.0-only */
#include "osos.h"
.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

.global cfw_menu_covers_query_original
.type cfw_menu_covers_query_original,%function
cfw_menu_covers_query_original:
    push {r4, r5, r6, lr}
    mov r5, r0
    ldr pc, =OSOS_MENU_ART_QUERY_RESUME
.ltorg
