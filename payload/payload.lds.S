/* SPDX-License-Identifier: GPL-3.0-only */

#include "layout.h"

ENTRY(__payload_start)
SECTIONS {
    . = CFW_PAYLOAD_BASE;
    __payload_start = .;
    __payload_limit = CFW_PAYLOAD_BASE + CFW_PAYLOAD_BYTES;
    __payload_file_offset = CFW_PAYLOAD_FILE_OFFSET;
    .text : { *(.text.entry) *(.text*) *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(4);
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }
    __payload_end = .;
    ASSERT(__payload_end <= CFW_PAYLOAD_BASE + CFW_PAYLOAD_BYTES,
           "CFW payload exceeds reserved memory")
    /DISCARD/ : { *(.cfw.patch) *(.comment) *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) }
}
