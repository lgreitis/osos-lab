/* SPDX-License-Identifier: GPL-3.0-only */

ENTRY(run_payload)
SECTIONS {
    . = 0x0bb20000;
    .text : { *(.text.entry) *(.text*) *(.rodata*) }
    ASSERT(execution_gate == 0x0bb20008, "Bds BLX target moved")
    .data : { *(.data*) }
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    ASSERT(SIZEOF(.bss) == 0, "Extension requires initialized data")
    ASSERT(. <= 0x0bb24000, "Extension overlaps Bds")

    . = 0x0bb24000;
    .bdsimage : { *(.bdsimage) }
    ASSERT(SIZEOF(.bdsimage) == 0x1500, "Bds length changed")

    . = 0x0bb25800;
    .versioncheck : { *(.versioncheck) }
    ASSERT(SIZEOF(.versioncheck) == 0x44, "Version predicate changed")

    . = 0x0bb25900;
    .sysinfo : { *(.sysinfo) }
    ASSERT(SIZEOF(.sysinfo) == 0x120, "SysInfo length changed")

    . = 0x0bb26000;
    .ososheader : { *(.ososheader) }
    ASSERT(SIZEOF(.ososheader) == 0x800, "OSOS header length changed")

    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) }
}
