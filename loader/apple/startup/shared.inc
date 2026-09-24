/* SPDX-License-Identifier: GPL-3.0-only */

.Lclean_data:
    mov r1, #0
.Lcache_way:
    mov r0, #0
.Lcache_index:
    orr r2, r1, r0
    mcr p15, 0, r2, c7, c14, 2
    add r0, r0, #32
    cmp r0, #4096
    bne .Lcache_index
    add r1, r1, #0x20000000
    cmp r1, #0
    bne .Lcache_way
    mcr p15, 0, r1, c7, c10, 4
    bx lr
.ltorg

/* Stop after a loader failure with interrupts already masked. */
.Lfinish:
    b .Lfinish
