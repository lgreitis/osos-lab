/* SPDX-License-Identifier: GPL-3.0-only */
#include "native/bindings.h"
.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4
.global cfw_bitmap_resource_get_original
.type cfw_bitmap_resource_get_original,%function
cfw_bitmap_resource_get_original:
    push {r3, r4, r5, r6, r7, lr}
    mov r5, r1
    ldr pc, =OSOS_RESOURCE_SERVICE_GET + 8
.ltorg
