/* SPDX-License-Identifier: GPL-3.0-only */

/* Extension module layout: linked at 0, text, rodata, data and bss in one
 * segment so only absolute relocations remain for rvx-pack; the loader adds the
 * load address to each. rv_module_entry is the module entry (rv_module_entry_fn). */
ENTRY(rv_module_entry)
SECTIONS {
    . = 0;
    .text : { *(.text.rv_module_entry) *(.text*) *(.rodata*) }
    . = ALIGN(4);
    __data_start = .;
    .data : { *(.data*) }
    . = ALIGN(4);
    __data_end = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    . = ALIGN(4);
    __bss_end = .;
    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) }
}
