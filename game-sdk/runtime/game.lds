ENTRY(game_start)

MEMORY
{
    game (rwx) : ORIGIN = 0x18000000, LENGTH = 1M
}

SECTIONS
{
    .eapp :
    {
        KEEP(*(.eapp))
    } > game

    .text :
    {
        *(.text.startup)
        *(.text*)
        *(.rodata*)
    } > game

    .ARM.extab :
    {
        *(.ARM.extab*)
    } > game

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > game

    .data :
    {
        *(.data*)
    } > game

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > game

    ASSERT(SIZEOF(.eapp) == 0x28, "Unexpected eapp header size")
    ASSERT(__bss_end <= ORIGIN(game) + LENGTH(game), "Game exceeds executable mapping")

    /DISCARD/ :
    {
        *(.comment)
        *(.ARM.attributes)
    }
}
