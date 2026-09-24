/* SPDX-License-Identifier: GPL-3.0-only */

.syntax unified
.cpu arm926ej-s
.section .text.entry,"ax",%progbits
.thumb
.thumb_func
.global run_native_payload
run_native_payload:
    bl native_resume
1:  b 1b
.arm
.section .text,"ax",%progbits

.global restore_import
.type restore_import,%function
restore_import:
    ldr r12, =0x0bb31000
    ldr r1, [r12, #64]
    msr spsr_cxsf, r1
    ldmia r12, {r0-r15}^ /* Exact registers, original SP and CPSR, PC=DXE+290. */

.global enter_osos
.type enter_osos,%function
enter_osos:
    mov r1, #0xd3 /* Existing Bds/ROM handoff expects IRQ and FIQ masked. */
    msr cpsr_c, r1
    ldr r12, =0x0bb20001
    bx r12

.global probe_finish
.type probe_finish,%function
probe_finish:
    ldr r12, =0x22039000
    ldr r12, [r12]
    bx r12

.global probe_flush
.type probe_flush,%function
probe_flush:
    ldr r12, =0x22039004
    ldr r12, [r12]
    bx r12
.ltorg
