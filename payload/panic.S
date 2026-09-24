#include "panic.h"

.syntax unified
.cpu arm926ej-s
.arm
.text

.global cfw_abort_entry
.type cfw_abort_entry,%function
cfw_abort_entry:
    /* Capture the abort caller before the C library changes its registers. */
    push {r0-r12, lr}
    mov r0, sp
    mov r1, #0x108
    b cfw_panic_entry
.size cfw_abort_entry, .-cfw_abort_entry

.global cfw_panic_entry
.type cfw_panic_entry,%function
cfw_panic_entry:
    /* Apple's vector wrapper supplies r0=register frame and r1=exception ID. */
    mrs r2, cpsr
    orr r3, r2, #0xc0
    msr cpsr_c, r3
    ldr r4, =cfw_panic_entered
    ldr r3, [r4]
    cmp r3, #0
    bne panic_halt
    mov r3, #1
    str r3, [r4]

    ldr r4, =cfw_panic_frame
    mov r5, r1
    and r1, r1, #0xff
    str r1, [r4, #PANIC_TYPE]
    str r2, [r4, #PANIC_CPSR]
    cmp r1, #8
    mrsne r3, spsr
    moveq r3, #0
    str r3, [r4, #PANIC_SPSR]
    beq panic_software
    mrc p15, 0, r3, c5, c0, 0
    str r3, [r4, #PANIC_DFSR]
    mrc p15, 0, r3, c6, c0, 0
    str r3, [r4, #PANIC_DFAR]
    mrc p15, 0, r3, c5, c0, 1
    str r3, [r4, #PANIC_IFSR]

    b panic_validate_frame
panic_software:
    tst r5, #0x100
    bne panic_validate_frame
    /* The common software-abort fallback supplies SP and a raw caller LR. */
    str lr, [r4, #PANIC_EXLR]
    str r0, [r4, #PANIC_SP]
    b panic_stack
panic_validate_frame:
    tst r0, #3
    bne panic_render
    cmp r0, #0x08000000
    blo panic_render
    ldr r2, =0x0c000000 - 56
    cmp r0, r2
    bls panic_copy
    cmp r0, #0x22000000
    blo panic_render
    ldr r2, =0x22040000 - 56
    cmp r0, r2
    bhi panic_render
panic_copy:
    mov r2, #0
1:
    ldr r3, [r0, r2]
    str r3, [r4, r2]
    add r2, r2, #4
    cmp r2, #56
    blo 1b
    mov r3, #1
    str r3, [r4, #PANIC_VALID]
    ldr r1, [r4, #PANIC_TYPE]
    cmp r1, #8
    bne panic_render
    add r0, r0, #56
    str r0, [r4, #PANIC_SP]
panic_stack:
    tst r0, #3
    bne panic_render
    mov r5, #0
    add r6, r4, #PANIC_STACK
panic_stack_word:
    cmp r0, #0x08000000
    blo panic_render
    ldr r2, =0x0c000000 - 4
    cmp r0, r2
    bls panic_stack_copy
    cmp r0, #0x22000000
    blo panic_render
    ldr r2, =0x22040000 - 4
    cmp r0, r2
    bhi panic_render
panic_stack_copy:
    ldr r3, [r0], #4
    str r3, [r6], #4
    add r5, r5, #1
    str r5, [r4, #PANIC_STACK_COUNT]
    cmp r5, #PANIC_STACK_WORDS
    blo panic_stack_word
panic_render:
    /* Use the payload's stack in SVC mode, including for faults from FIQ. */
    msr cpsr_c, #0xd3
    ldr sp, =cfw_panic_stack_top
    ldr r0, =cfw_panic_frame
    bl cfw_panic_screen
panic_halt:
    ldr r0, =0x3c800000
    mov r1, #0
    str r1, [r0]
2:
    b 2b
.size cfw_panic_entry, .-cfw_panic_entry
.ltorg

.section .bss.panic,"aw",%nobits
.balign 8
cfw_panic_entered:
    .space 4

.global cfw_panic_frame
cfw_panic_frame:
    .space PANIC_FRAME_BYTES
.balign 8
cfw_panic_stack:
    .space 4096
cfw_panic_stack_top:
