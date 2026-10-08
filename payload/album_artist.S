/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"

.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

.global cfw_album_artist_import_hook
.type cfw_album_artist_import_hook,%function
cfw_album_artist_import_hook:
    /* Inside ImportTrackDataset: r7 = reader, r4 = record; SP is its native frame. */
    ldr r2, [sp, #0x228]
    ldr r3, [sp, #0x224]
    sub r2, r2, r3
    mov r0, r7
    mov r1, r4
    bl cfw_album_artist_import
    ldr pc, =OSOS_ITUNES_IMPORT_STATUS_RESUME
.ltorg
