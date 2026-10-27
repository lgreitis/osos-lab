/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"
#include "patches/sites.h"

.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

/* Replay the two instructions replaced at the playlist name getter entry. movs sets
   the flags the native body tests right after (popeq), and ldr pc keeps them. */
.global cfw_playlist_get_name_original
.type cfw_playlist_get_name_original,%function
cfw_playlist_get_name_original:
    movs r2, r1
    mov r3, r0
    ldr pc, =OSOS_PLAYLIST_GET_NAME_RESUME
.ltorg
