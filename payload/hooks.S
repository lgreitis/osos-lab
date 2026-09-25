#include "osos.h"

.syntax unified
.cpu arm926ej-s
.arm
.section .text.entry,"ax",%progbits

.global cfw_resource_hook
.type cfw_resource_hook,%function
cfw_resource_hook:
    push {r4, lr}
    mov r4, r0
    /* Create Apple's override map before registering CFW resources. */
    ldr r12, =OSOS_RESOURCE_BANK_INIT_OVERRIDES
    blx r12
    mov r0, r4
    bl cfw_install_resources
    pop {r4, pc}
.ltorg

.section .text,"ax",%progbits
.balign 4

.global cfw_eq_load_hook
.type cfw_eq_load_hook,%function
cfw_eq_load_hook:
    ldr r12, =cfw_eq_load
    bx r12
.ltorg

.global cfw_eq_process_hook
.type cfw_eq_process_hook,%function
cfw_eq_process_hook:
    ldr r12, =cfw_eq_process
    bx r12
.ltorg

.global cfw_preferences_hook
.type cfw_preferences_hook,%function
cfw_preferences_hook:
    push {r4, lr}
    ldr r12, =0x0809c5e8
    blx r12
    bl cfw_eq_load_preferences
    pop {r4, pc}
.ltorg

/* Settings IDs 100..122 map to DSP presets 1..23. */

.global cfw_eq_map_hook
.type cfw_eq_map_hook,%function
cfw_eq_map_hook:
    sub r0, r0, #100
    cmp r0, #22
    addls r0, r0, #1
    movhi r0, #0
    bx lr

/* Preserve Flat's override behavior and apply it to Custom too. */

.global cfw_eq_track_guard
.type cfw_eq_track_guard,%function
cfw_eq_track_guard:
    cmp r0, #107
    cmpne r0, #122
    bx lr

.global cfw_irq_save
.type cfw_irq_save,%function
cfw_irq_save:
    mrs r0, cpsr
    orr r1, r0, #0xc0
    msr cpsr_c, r1
    bx lr

.global cfw_irq_restore
.type cfw_irq_restore,%function
cfw_irq_restore:
    msr cpsr_c, r0
    bx lr

.section .text,"ax",%progbits

.global cfw_template_hook
.type cfw_template_hook,%function
cfw_template_hook:
    ldr r12, =cfw_next_template
    bx r12
.ltorg

.global cfw_settings_action_hook
.type cfw_settings_action_hook,%function
cfw_settings_action_hook:
    ldr r12, =cfw_settings_action
    bx r12
.ltorg

.balign 4
.global cfw_game_manifest_hook
.type cfw_game_manifest_hook,%function
cfw_game_manifest_hook:
    ldr r12, =cfw_game_manifest_reader
    bx r12
.ltorg
