/* SPDX-License-Identifier: GPL-3.0-only */

.syntax unified
.cpu arm926ej-s
.arm
.section .text.entry,"ax",%progbits
.equ RESULT, 0x2203c000
.equ SCRATCH, 0x2203c100
.equ STACK, 0x2203b000

.macro call target
    adr lr, .Lret\@
    b \target
.Lret\@:
.endm

.macro stage number
    ldr r10, =RESULT
    mov r0, #\number
    str r0, [r10, #12]
.endm

.global probe_start
.type probe_start,%function
probe_start:
    mov r12, #0xd3
    msr cpsr_c, r12
    ldr sp, =STACK
    push {r0-r3}
    call .Lclean_data
    ldr r0, =0x00050078
    mcr p15, 0, r0, c1, c0, 0
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0
    mcr p15, 0, r0, c8, c7, 0
    mcr p15, 0, r0, c13, c0, 0
    ldr r10, =RESULT
    mov r1, #0
    mov r2, #256
1:  subs r2, r2, #4
    str r1, [r10, r2]
    bne 1b
    ldr r0, =0x31455844 /* DXE1 */
    str r0, [r10]
    mov r0, #1
    str r0, [r10, #4]
    stage 1
    pop {r0-r3}
    blx prepare_files
    /* Preserve Rockbox-preloaded DRAM; original PreEfi initializes hardware. */
    stage 4
    ldr r12, =0x2201054d
    blx r12
    b .Lapple_error /* An unexpected return enters the fault handler. */

.global probe_exception
.type probe_exception,%function
probe_exception:
    mov r0, #0xd3
    msr cpsr_c, r0
    ldr sp, =STACK
    mov r0, #6
    b probe_finish
.Lapple_error:
    ldr r10, =RESULT
    str r0, [r10, #76]
    mov r0, #4
    b probe_finish
.Lwarm_mode:
    mov r0, #5
    b probe_finish

.global probe_finish
.type probe_finish,%function
probe_finish:
    /* Switch to the startup stack with interrupts masked. */
    mov r1, #0xd3
    msr cpsr_c, r1
    ldr sp, =STACK
    b .Lfinish

.global probe_flush
.type probe_flush,%function
probe_flush:
    push {r4, lr}
    call .Lclean_data
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0
    mcr p15, 0, r0, c7, c10, 4
    pop {r4, pc}
.ltorg

.thumb
.thumb_func
.global cold_guard
cold_guard:
    cmp r0, #0
    beq 3f
    ldr r3, =.Lwarm_mode
    bx r3
3:  mov r2, sp /* Replay the exact overwritten original instructions. */
    add r1, sp, #8
    bx lr
.ltorg
.arm
.include "shared.inc"

/* Save DXE state before the ROM preparation pass. */
.syntax unified
.cpu arm926ej-s
.arm
.section .text,"ax",%progbits

.global import_capture
.type import_capture,%function
import_capture:
    stmdb sp!, {r0-r12, lr}
    ldr r0, =0x0bb31000
    mrs r3, cpsr
    orr r3, r3, #0x20 /* Resume the original Thumb continuation. */
    str r3, [r0, #64]
    mov r1, sp
    mov r2, #13
1:  ldr r3, [r1], #4
    str r3, [r0], #4
    subs r2, r2, #1
    bne 1b
    ldr r3, [r1] /* Preserve LR; a callee returning via POP PC may leave it changed. */
    str r3, [r0, #4]
    ldr r3, =0x2203c060 /* DXE base recorded by before_dxe. */
    ldr r3, [r3]
    add r3, r3, #0x290
    str r3, [r0, #8]
    add r3, sp, #56
    str r3, [r0]
    ldr r0, [sp]
    add sp, sp, #56
    ldr r3, =after_dxe
    bx r3
.ltorg
