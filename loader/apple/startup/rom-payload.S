/* SPDX-License-Identifier: GPL-3.0-only */

.syntax unified
.cpu arm926ej-s
.arm
.section .romimage,"ax",%progbits

.global rom_image
rom_image:
.incbin "ROM-relocated.bin",0,0x6b4
.thumb
.thumb_func
.global rom_gate_patch
rom_gate_patch:
    bl rom_plaintext_gate
    movs r6, #0
    b rom_cleanup
.incbin "ROM-relocated.bin",0x6bc,0x1a
rom_cleanup:
.incbin "ROM-relocated.bin",0x6d6,0xf0a
.section .clockimage,"ax",%progbits
.incbin "ClockAndReset-relocated.bin"
.section .cpuimage,"ax",%progbits
.incbin "Cpu-relocated.bin"
.section .irqimage,"ax",%progbits
.incbin "InterruptController-relocated.bin"
