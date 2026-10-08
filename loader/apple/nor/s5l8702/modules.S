/* SPDX-License-Identifier: GPL-3.0-only */

.syntax unified
.cpu arm926ej-s
#include "common/recipe.inc"
#include "layout.h"
.arm
.section .romimage,"ax",%progbits

.global rom_image
rom_image:
native_module ROM, APPLE_ROM_BYTES, 21
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
native_module ClockAndReset, APPLE_CLOCK_BYTES, 10
native_copy ClockAndReset, 0, APPLE_CLOCK_BYTES
.section .cpuimage,"ax",%progbits
native_module Cpu, APPLE_CPU_BYTES, 26
native_copy Cpu, 0, APPLE_CPU_BYTES
.section .irqimage,"ax",%progbits
native_module InterruptController, APPLE_IRQ_BYTES, 17
native_copy InterruptController, 0, APPLE_IRQ_BYTES
