/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"

.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

.global cfw_irq_save
.type cfw_irq_save,%function
cfw_irq_save:
    mrs r0, cpsr
    orr r1, r0, #0xc0
    msr cpsr_c, r1
    bx lr

.global cfw_irq_restore
.type cfw_irq_restore,%function
cfw_irq_restore:
    msr cpsr_c, r0
    bx lr
