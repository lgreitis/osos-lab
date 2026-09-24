/* SPDX-License-Identifier: GPL-3.0-only */

.syntax unified
.cpu arm926ej-s
.section .text.entry,"ax",%progbits
.thumb
.thumb_func
.global run_payload
run_payload:
    bl load_osos
1:  b 1b

/* Called by BLX replacing only the original CPU-mode call. Keep Bds's
 * original BLX r4 and caller register/stack state. All post-check code uses
 * identity physical addresses, including stack and original Cpu helpers.
 */
.balign 4
.arm
.type execution_gate,%function

.global execution_gate
execution_gate:
    push {r0-r3, r12, lr}
    mov r0, r4
    ldr r12, =after_bds
    blx r12
    /* Clean/invalidate all D-cache and drain using original Cpu+6a8. */
    ldr r12, =0x2201e828
    blx r12
    /* Cpu private protocol+0 is Cpu+33b -> +704, mask0x1005:
     * turn off I-cache, D-cache, MMU, preserving other control bits.
     * Delay until here so the passed loader can use its uncached aliases.
     */
    ldr r0, =0x1005
    ldr r12, =0x2201e884
    blx r12
    /* Original I-cache invalidation/drain helper, Cpu+6f0. */
    ldr r12, =0x2201e870
    blx r12
    pop {r0-r3, r12, lr}
    b original_cpu_mode

.global execution_gate_end
execution_gate_end:
.ltorg

.section .bdsimage,"ax",%progbits

.global bds_image
bds_image:
.incbin "bds-relocated.bin",0,0x4ce
.thumb
.global bds_execution_patch
bds_execution_patch:
    /* Fixed-layout Thumb-1 BLX into execution_gate. */
    .hword 0xf000 | (((0x0bb20008 - 0x0bb244d0) >> 12) & 0x7ff)
    .hword 0xe800 | (((0x0bb20008 - 0x0bb244d0) >> 1) & 0x7fe)
.incbin "bds-relocated.bin",0x4d2,0xd4a
.arm
.global original_cpu_mode
original_cpu_mode:
.incbin "bds-relocated.bin",0x121c,0x2e4

.section .versioncheck,"ax",%progbits
.thumb
.thumb_func
.global original_version_check
original_version_check:
.incbin "software-version.bin"

.section .sysinfo,"a",%progbits
.incbin "sysinfo.bin"
.section .ososheader,"a",%progbits
.incbin "osos-header.bin"
