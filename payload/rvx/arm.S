/* SPDX-License-Identifier: GPL-3.0-only */
#include "host.h"

.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

.global cfw_rvx_lock
.type cfw_rvx_lock,%function
cfw_rvx_lock:
    mrs r0, cpsr
    orr r1, r0, #0xc0
    msr cpsr_c, r1
    bx lr
.global cfw_rvx_unlock
.type cfw_rvx_unlock,%function
cfw_rvx_unlock:
    msr cpsr_c, r0
    bx lr

.global cfw_rvx_sync
.type cfw_rvx_sync,%function
cfw_rvx_sync:
    bic r0, r0, #31
1:  mcr p15, 0, r0, c7, c10, 1
    add r0, r0, #32
    cmp r0, r1
    blo 1b
    mov r0, #0
    mcr p15, 0, r0, c7, c10, 4
    mcr p15, 0, r0, c7, c5, 0
    bx lr

/* ARM926 section mappings: use the uncached DRAM alias for a read-only section.
 * Clean and invalidate the cached line first so it cannot overwrite the new word. */
.global cfw_rvx_write
.type cfw_rvx_write,%function
cfw_rvx_write:
    mrc p15, 0, r2, c2, c0, 0
    mov r2, r2, lsr #14
    mov r2, r2, lsl #14
    mov r3, r0, lsr #20
    ldr r2, [r2, r3, lsl #2]
    tst r2, #0xc00
    bne 2f
    mcr p15, 0, r0, c7, c14, 1
    mcr p15, 0, r0, c7, c10, 4
    orr r2, r0, #0x80000000
    str r1, [r2]
    b 3f
2:  str r1, [r0]
    mcr p15, 0, r0, c7, c10, 1
3:  mcr p15, 0, r0, c7, c10, 4
    mcr p15, 0, r0, c7, c5, 0
    bx lr


/* One ARM entry per slot. Saving the whole register set also exposes arguments
 * that specific stock call sites keep in callee-saved registers. */
.global rvx_vectors
rvx_vectors:
.rept RVX_HOOK_SLOTS
    push {r0-r12,lr}
    bl .Ldispatch
.endr
.Ldispatch:
    /* The vector saved the original LR. Its own return address identifies its slot. */
    ldr r1, =rvx_vectors+8
    sub r1, lr, r1
    mov r1, r1, lsr #3
    mov r0,sp
    bl rvx_dispatch
    str r0,[sp]
    pop {r0-r12,pc}

/* This observation site replaces a store in the middle of the importer.
 * Its caller still owns every register, its return address and NZCV. */
.global rvx_insert_vector
.type rvx_insert_vector,%function
rvx_insert_vector:
    push {r0-r12, lr}
    mov r0, sp
    mrs r2, cpsr
    push {r2,r3}
    ldr r1, =rvx_insert_slot
    ldr r1, [r1]
    bl rvx_dispatch
    pop {r2,r3}
    msr cpsr_f, r2
    pop {r0-r12,lr}
    ldr pc, =0x0808a86c

.section .rodata
.balign 4
.global rvx_trailer
rvx_trailer:
    .word 0,0,0,0
