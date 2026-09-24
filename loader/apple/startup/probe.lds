/* SPDX-License-Identifier: GPL-3.0-only */

ENTRY(probe_start)
SECTIONS {
    . = 0x2201a730;
    .text : { *(.text.entry) *(.text*) *(.rodata*) }
    .data : { *(.data*) }
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    ASSERT(SIZEOF(.bss) == 0, "Startup requires initialized data")
    ASSERT(. <= 0x2201c300, "Helpers overlap ROM")

    . = 0x2201c300;
    .romimage : { *(.romimage) }
    ASSERT(SIZEOF(.romimage) == 0x15e0, "ROM size changed")

    . = 0x2201d900;
    .clockimage : { *(.clockimage) }
    ASSERT(SIZEOF(.clockimage) == 0x840, "ClockAndReset size changed")

    . = 0x2201e180;
    .cpuimage : { *(.cpuimage) }
    ASSERT(SIZEOF(.cpuimage) == 0x9e0, "Cpu size changed")

    . = 0x2201eb80;
    .irqimage : { *(.irqimage) }
    ASSERT(SIZEOF(.irqimage) == 0x8e0, "InterruptController size changed")
    ASSERT(. <= 0x2201f510, "Startup overlaps SecCore")

    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) }
}
