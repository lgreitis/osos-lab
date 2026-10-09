/* SPDX-License-Identifier: GPL-3.0-only */

#include "layout.h"

ENTRY(run_payload)
SECTIONS {
    . = 0x0bb20000;
    image_start = .;
    .text : { *(.text.entry) *(.text*) *(.rodata*) }
    ASSERT(execution_gate == 0x0bb20008, "Bds BLX target moved")
    ASSERT(bds_execution_patch == 0x0bb244ce, "Bds BLX source moved")
    .data : { *(.data*) }
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    ASSERT(SIZEOF(.bss) == 0, "Extension requires initialized data")
    ASSERT(. <= APPLE_BDS_BASE, "Extension overlaps Bds")

    . = APPLE_BDS_BASE;
    .bdsimage : { *(.bdsimage) }
    ASSERT(SIZEOF(.bdsimage) == APPLE_BDS_BYTES, "Bds length changed")

    . = APPLE_VERSION_BASE;
    .versioncheck : { *(.versioncheck) }
    ASSERT(SIZEOF(.versioncheck) == 0x44, "Version predicate changed")

    . = 0x0bb25900;
    .sysinfo : { *(.sysinfo) }
    ASSERT(SIZEOF(.sysinfo) == 0x120, "SysInfo length changed")

    . = 0x0bb26000;
    .ososheader : { *(.ososheader) }
    ASSERT(SIZEOF(.ososheader) == 0x800, "OSOS header length changed")

    image_limit = 0x0bb28000;
    .cfw.native 0 : { *(.cfw.native) }

    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) }
}
