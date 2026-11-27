/* SPDX-License-Identifier: MIT */

/* A module: linked at 0, the table first, text, rodata, data and bss in one
 * piece, so the only words to fix at load are absolute addresses, which the
 * loader moves by the block's address. The image's symbols come in through
 * --just-symbols, so names here stay apart from eapp.lds's. */

ENTRY(eapp_module)

SECTIONS
{
    . = 0;
    .text :
    {
        KEEP(*(.eapp_module))
        *(.text*)
        *(.rodata*)
    }
    . = ALIGN(4);
    .data :
    {
        *(.data*)
    }
    . = ALIGN(4);
    __eapp_module_bss_start = .;
    .bss (NOLOAD) :
    {
        *(.bss*)
        *(COMMON)
    }
    . = ALIGN(4);
    __eapp_module_bss_end = .;

    .init_array :
    {
        *(.init_array*)
    }

    ASSERT(eapp_module == 0, "the module table must come first")
    ASSERT(SIZEOF(.init_array) == 0, "a module runs no constructors")

    /DISCARD/ :
    {
        *(.comment)
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}
