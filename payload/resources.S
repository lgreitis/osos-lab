/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"

.syntax unified
.cpu arm926ej-s
.arm
.section .text.entry,"ax",%progbits

.global cfw_resource_hook
.type cfw_resource_hook,%function
cfw_resource_hook:
    push {r4, lr}
    mov r4, r0
    /* Create Apple's override map before registering CFW resources. */
    ldr r12, =OSOS_RESOURCE_BANK_INIT_OVERRIDES
    blx r12
    mov r0, r4
    bl cfw_install_resources
    pop {r4, pc}
.ltorg

.global cfw_resource_service_get_original
.type cfw_resource_service_get_original,%function
cfw_resource_service_get_original:
    push {r3, r4, r5, r6, r7, lr}
    mov r5, r1
    ldr pc, =OSOS_RESOURCE_SERVICE_GET + 8
.ltorg
