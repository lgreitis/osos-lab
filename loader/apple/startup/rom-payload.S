/* SPDX-License-Identifier: GPL-3.0-only */

.syntax unified
.cpu arm926ej-s
#include "common/recipe.inc"
.arm
.section .romimage,"ax",%progbits

.global rom_image
rom_image:
native_module ROM, 0x15e0, 21
native_copy ROM, 0, 0x6b4
native_expect ROM, 0x6b4, 0x98, 0x47, 0x02, 0x00, 0x0a, 0xd0, 0x8d, 0x48
.thumb
.thumb_func
.global rom_gate_patch
rom_gate_patch:
    bl rom_plaintext_gate
    movs r6, #0
    b rom_cleanup
native_copy ROM, 0x6bc, 0x1a
rom_cleanup:
native_copy ROM, 0x6d6, 0xf0a
.section .clockimage,"ax",%progbits
native_module ClockAndReset, 0x840, 10
native_copy ClockAndReset, 0, 0x840
.section .cpuimage,"ax",%progbits
native_module Cpu, 0x9e0, 26
native_copy Cpu, 0, 0x9e0
.section .irqimage,"ax",%progbits
native_module InterruptController, 0x8e0, 17
native_copy InterruptController, 0, 0x8e0
