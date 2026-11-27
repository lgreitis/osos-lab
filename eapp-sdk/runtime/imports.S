/* SPDX-License-Identifier: MIT */

/* The image's import list, what the eApp header's imports word points at and
 * the loader resolves against the host's registry. A record is the interface's
 * name in 32 bytes, its id, the function count, the next record and one stub
 * per function: a jump through the word after it, which the loader fills with
 * the host's entry. A record ends in the stub marker; the list ends in a record
 * with the host's end-of-list name. */

.syntax unified
.cpu arm926ej-s
.arm

/* The interface ids and counts of the symbols header, extracted by eapp.mk. */
#include "imports_ids.h"

.section .text.imports,"ax",%progbits
.balign 4
.global eapp_imports
eapp_imports:

/* `interface` is the name the host matches, `symbol` its spelling in the
 * symbols header. */
#define EAPP_IMPORT_INTERFACE(interface, symbol) \
    eapp_import_##interface: .ascii #interface; \
    .space GameAPIModule_NameBytes - (. - eapp_import_##interface); \
    .word GameAPI_##symbol##_Id0, GameAPI_##symbol##_Id1, \
          GameAPI_##symbol##_Id2, GameAPI_##symbol##_Id3; \
    .word GameAPI_##symbol##_Count; \
    .word eapp_import_end_##interface + 4; \
    .set eapp_import_count_##interface, GameAPI_##symbol##_Count; \
    eapp_import_stubs_##interface:

#define EAPP_IMPORT(name) \
    .global name; \
    .type name, %function; \
    name: ldr pc, [pc, #-4]; \
    .word 0; \
    .size name, 8;

/* The count is the stubs emitted; the assembler checks it against the header's. */
#define EAPP_IMPORT_INTERFACE_END(interface) \
    eapp_import_end_##interface: .word GameImport_Tail; \
    .if (eapp_import_end_##interface - eapp_import_stubs_##interface) / 8 - eapp_import_count_##interface; \
    .error "import count"; \
    .endif;

#include "imports.def"

/* GameImport_EndOfListName, as the loader compares it. */
eapp_import_end: .ascii "$$$$ a^n + b^n = c^n | n>2 $$$$"
.space GameAPIModule_NameBytes - (. - eapp_import_end)
.word 0, 0, 0, 0
.word 0
.word 0
.word GameImport_Tail

.section .note.GNU-stack,"",%progbits
