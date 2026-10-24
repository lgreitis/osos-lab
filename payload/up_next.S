/* SPDX-License-Identifier: GPL-3.0-only */
/* Up Next: resume stubs for hooked native functions (re-run the two replaced
   instructions, then continue in the original). */
.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

.global cfw_build_songs_original
.type cfw_build_songs_original,%function
cfw_build_songs_original:          @ MediaBrowser_BuildSongs 0x0807b858
    push {r0, r4, r5, r6, r7, r8, r9, r10, r11, lr}
    sub sp, sp, #0x18
    ldr pc, =0x0807b860

.global cfw_song_menu_original
.type cfw_song_menu_original,%function
cfw_song_menu_original:            @ SongList_BuildAndShowContextualMenu 0x08219478
    push {r4, r5, r6, r7, r8, r9, r10, r11, lr}
    sub sp, sp, #0x34
    ldr pc, =0x08219480
.global cfw_play_row_original
.type cfw_play_row_original,%function
cfw_play_row_original:             @ MediaListController_PlaySongRow 0x0820fc54
    push {r4, r5, r6, r7, r8, r9, r10, r11, lr}
    mov r6, r0
    ldr pc, =0x0820fc5c
.ltorg

.global cfw_pop_top_original
.type cfw_pop_top_original,%function
cfw_pop_top_original:              @ navigator pop top screen 0x082896b8
    push {r4, r5, r6, lr}
    mov r5, r0
    ldr pc, =0x082896c0
.ltorg

.global cfw_np_event_original
.type cfw_np_event_original,%function
cfw_np_event_original:             @ NowPlayingModel event handler 0x081efe74
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0
    ldr pc, =0x081efe7c
.ltorg
