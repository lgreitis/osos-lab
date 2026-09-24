/* SPDX-License-Identifier: GPL-3.0-only */

.syntax unified
.cpu arm926ej-s
.thumb
.section .text,"ax",%progbits

.global native_image_entry
.type native_image_entry,%function
.thumb_func
native_image_entry:
    push {r4, lr}
    movs r0, r4
    bl native_prepare_image
    pop {r4}
    pop {r3}
    /* Replay all four instructions replaced at DXE+5898. */
    movs r0, #1
    strb r0, [r4, #0xc]
    ldr r2, [r4, #0x10]
    ldr r1, [r4, #0x20]
    ldr r0, [sp, #8]
    ldr r3, =0x0bb3001c
    ldr r3, [r3]
    bx r3
.ltorg

.global native_stop
.type native_stop,%function
.thumb_func
native_stop:
    /* Continue with the original DXE stack. */
    bl native_report
1:  b 1b
.ltorg
