/* SPDX-License-Identifier: GPL-3.0-only */

ENTRY(run_native_payload)
SECTIONS {
    . = 0x0bb28000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) }
    .data : { *(.data*) }
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }

    ASSERT(SIZEOF(.bss) == 0, "Native helper requires initialized data")
    ASSERT(. <= 0x0bb2c000, "Native helper overlaps state")

    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) }
}
