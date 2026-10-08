/* SPDX-License-Identifier: GPL-3.0-only */

#include "layout.h"

ENTRY(probe_start)
SECTIONS {
    . = APPLE_STARTUP_BASE;
    image_start = .;
    .text : { *(.text.entry) *(.text*) *(.rodata*) }
    .data : { *(.data*) }
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    ASSERT(SIZEOF(.bss) == 0, "Startup requires initialized data")
    ASSERT(. <= APPLE_ROM_BASE, "Helpers overlap ROM")

    . = APPLE_ROM_BASE;
    .romimage : { *(.romimage) }
    ASSERT(SIZEOF(.romimage) == APPLE_ROM_BYTES, "ROM size changed")

    . = APPLE_CLOCK_BASE;
    .clockimage : { *(.clockimage) }
    ASSERT(SIZEOF(.clockimage) == APPLE_CLOCK_BYTES, "ClockAndReset size changed")

    . = APPLE_CPU_BASE;
    .cpuimage : { *(.cpuimage) }
    ASSERT(SIZEOF(.cpuimage) == APPLE_CPU_BYTES, "Cpu size changed")

    . = APPLE_IRQ_BASE;
    .irqimage : { *(.irqimage) }
    ASSERT(SIZEOF(.irqimage) == APPLE_IRQ_BYTES, "InterruptController size changed")
    ASSERT(. <= APPLE_STARTUP_LIMIT, "Startup overlaps SecCore")

    image_limit = APPLE_STARTUP_LIMIT;
    apple_loader_base = APPLE_LOADER_BASE;
    apple_loader_bytes = APPLE_LOADER_BYTES;
    .cfw.native 0 : { *(.cfw.native) }

    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) }
}
