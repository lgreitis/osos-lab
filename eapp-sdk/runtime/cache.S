/* SPDX-License-Identifier: MIT */

/* The ARM926EJ-S keeps separate 32-byte-line instruction and data caches, so
 * code written through the data cache runs only once its lines are cleaned to
 * memory and the instruction cache forgets what it held. The host runs apps
 * and games in SVC mode, where these CP15 operations are allowed. */

.syntax unified
.cpu arm926ej-s
.arm

/* void eapp_cache_sync_code(const void *start, size_t bytes) */
.section .text.eapp_cache_sync_code,"ax",%progbits
.balign 4
.global eapp_cache_sync_code
.type eapp_cache_sync_code,%function
eapp_cache_sync_code:
    add r1, r0, r1
    bic r0, r0, #31
1:
    cmp r0, r1
    mcrlo p15, 0, r0, c7, c10, 1    /* clean the data cache line */
    addlo r0, r0, #32
    blo 1b
    mov r0, #0
    mcr p15, 0, r0, c7, c10, 4      /* drain the write buffer */
    mcr p15, 0, r0, c7, c5, 0       /* invalidate the instruction cache */
    bx lr
.size eapp_cache_sync_code, .-eapp_cache_sync_code

.section .note.GNU-stack,"",%progbits
