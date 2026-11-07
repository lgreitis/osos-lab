/* SPDX-License-Identifier: GPL-3.0-only */

.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

.global cfw_artwork_track_hook
.type cfw_artwork_track_hook,%function
cfw_artwork_track_hook:
    /* Replaces `str r0, [r4, #0xf0]` in the mhit parser: r0 = image ID, r4 = track. */
    push {r0-r3, r12, lr}
    mov r1, r4
    bl cfw_artwork_track_image
    pop {r0-r3, r12, pc}
