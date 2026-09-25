.syntax unified
.cpu arm926ej-s
.arm

.section .eapp,"a",%progbits
.balign 4
.global game_eapp_header
game_eapp_header:
    .word 0x70706165
    .word 0x10001000
    .word 5
    .word 0
    .word 0
    .word game_start
    .word game_runtime_shutdown
    .word game_reserved_entry
    .word game_reserved_entry
    .word game_runtime_frame

.section .text.startup,"ax",%progbits
.balign 4
.global game_start
.type game_start,%function
game_start:
    /* osos owns the game task and its stack. */
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
1:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 1b
    b game_runtime_init
.size game_start, .-game_start
.ltorg

.type game_reserved_entry,%function
game_reserved_entry:
    mov r0, #0
    bx lr
.size game_reserved_entry, .-game_reserved_entry

.section .note.GNU-stack,"",%progbits
