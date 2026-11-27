/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/runtime/startup.S (8fc840d) by lgreitis. */

.syntax unified
.cpu arm926ej-s
.arm

.section .text.startup,"ax",%progbits
.balign 4
.global eapp_start
.type eapp_start,%function
eapp_start:
    /* The host owns the task and its stack; only bss needs clearing. */
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
1:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 1b
    b eapp_runtime_init
.size eapp_start, .-eapp_start
.ltorg

.global eapp_reserved_entry
.type eapp_reserved_entry,%function
eapp_reserved_entry:
    mov r0, #0
    bx lr
.size eapp_reserved_entry, .-eapp_reserved_entry

.section .note.GNU-stack,"",%progbits
