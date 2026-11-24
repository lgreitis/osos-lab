# SPDX-License-Identifier: GPL-3.0-only
# Builds one extension into OUT/<id>.rvx from its manifest.toml: every module in
# the manifest's order (main first), the libraries it requires and its assets.
# A Makefile names each module's sources and includes this file:
#
#   main_SOURCES := main.c       # C and assembly sources of module `main`, as Thumb
#   main_ARM_SOURCES := dsp.c    # optional: compiled as ARM
#   EXTRA_CFLAGS := -I../shared  # optional, set before the include
#   LIBRARY_PATH := ..           # optional: where the required libraries' directories are
#   include $(or $(SDK),$(error set SDK to an unpacked revola SDK))/rv.mk
#
# `make check` runs tests/run.py with the SDK when the extension has one.

SDK ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
CROSS_COMPILE ?= arm-elf-eabi-
OUT ?= build
PYTHON ?= python3 -B
include $(SDK)/rv_firmware.mk
SDK_SOURCES := rv_libc.c rv_canvas.c
SDK_OBJECTS := $(patsubst %.c,$(OUT)/%.o,$(SDK_SOURCES))

# The id, modules, required libraries and assets, as the manifest says; not
# read for `make clean`, which must work whatever the manifest holds.
$(OUT)/manifest.mk: manifest.toml $(SDK)/manifest.py | $(OUT)
	$(PYTHON) $(SDK)/manifest.py makefile $< > $@
ifneq ($(filter-out clean,$(or $(MAKECMDGOALS),all)),)
include $(OUT)/manifest.mk
endif
ID := $(MANIFEST_ID)
MODULES := $(MANIFEST_MODULES)
LIBRARY_PATH ?= ..
LIBRARIES := $(addprefix $(LIBRARY_PATH)/,$(MANIFEST_REQUIRES))
ASSETS ?= $(MANIFEST_ASSETS)
LIBRARY_MANIFESTS := $(addsuffix /manifest.toml,$(LIBRARIES))

CC := $(CROSS_COMPILE)gcc
CFLAGS := -mcpu=arm926ej-s -mthumb -mthumb-interwork -mlong-calls -Os -std=c11 \
    -ffreestanding -fno-builtin -fno-common -fno-pic -fno-jump-tables \
    -fno-unwind-tables -fno-asynchronous-unwind-tables -Wall -Wextra -Werror \
    -Wno-missing-field-initializers \
    -I$(SDK) $(addprefix -I,$(LIBRARIES)) $(EXTRA_CFLAGS)
ARM_CFLAGS := $(filter-out -mthumb,$(CFLAGS)) -marm -fno-optimize-sibling-calls
LDFLAGS := -nostdlib -Wl,-T,$(SDK)/rvx.lds -Wl,--emit-relocs -Wl,--gc-sections \
    -Wl,--just-symbols=$(SDK)/$(RV_FIRMWARE_LD) -lgcc

# A library's own exports stay through --gc-sections; the exports of the
# required libraries link as 0, and rvx-pack turns the words that name them
# into imports the core fills in.
SCRIPTS := $(OUT)/exports.ld
$(OUT)/exports.ld: manifest.toml | $(OUT)
	$(PYTHON) $(SDK)/manifest.py exports $< > $@
ifneq ($(LIBRARIES),)
SCRIPTS += $(OUT)/imports.ld
$(OUT)/imports.ld: manifest.toml $(LIBRARY_MANIFESTS) | $(OUT)
	$(PYTHON) $(SDK)/manifest.py imports $(LIBRARY_MANIFESTS) > $@
endif
comma := ,
LDFLAGS += $(addprefix -Wl$(comma)-T$(comma),$(SCRIPTS))

# One object per source, with the headers it includes as dependencies.
define module_rules
$(1)_OBJECTS := $$(patsubst %,$(OUT)/$(1)/%.o,$$($(1)_SOURCES)) \
    $$(patsubst %,$(OUT)/$(1)-arm/%.o,$$($(1)_ARM_SOURCES))
$(OUT)/$(1).elf: $$($(1)_OBJECTS) $(SDK_OBJECTS) $(SDK)/rvx.lds $(SCRIPTS) | $(OUT)
	$(CC) $(CFLAGS) $$(filter %.o,$$^) $(LDFLAGS) -Wl,-Map,$$(@:.elf=.map) -o $$@
$(OUT)/$(1)/%.o: %
	@mkdir -p $$(dir $$@)
	$(CC) $(CFLAGS) -MMD -MP -c $$< -o $$@
$(OUT)/$(1)-arm/%.o: %
	@mkdir -p $$(dir $$@)
	$(CC) $(ARM_CFLAGS) -MMD -MP -c $$< -o $$@
-include $$($(1)_OBJECTS:.o=.d)
endef
$(foreach module,$(MODULES),$(eval $(call module_rules,$(module))))

# The SDK's own sources go into every module, each function and object in its
# own section, so --gc-sections keeps only those used.
define sdk_rules
$(OUT)/$(1).o: $(SDK)/$(1).c | $(OUT)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -fno-tree-loop-distribute-patterns \
	    -MMD -MP -c $$< -o $$@
-include $(OUT)/$(1).d
endef
$(foreach source,$(SDK_SOURCES),$(eval $(call sdk_rules,$(basename $(source)))))

all: $(OUT)/$(ID).rvx

ELVES := $(addprefix $(OUT)/,$(addsuffix .elf,$(MODULES)))
$(OUT)/$(ID).rvx: manifest.toml $(ELVES) $(ASSETS) $(LIBRARY_MANIFESTS)
	$(PYTHON) $(SDK)/rvx-pack $@ $< $(ELVES) --prefix $(CROSS_COMPILE) \
	    --api $(RV_API) --firmware $(RV_FIRMWARE_ID) \
	    $(addprefix --library ,$(LIBRARY_MANIFESTS))

$(OUT):
	mkdir -p $@

clean:
	rm -rf $(OUT)

ifneq ($(wildcard tests/run.py),)
check:
	$(PYTHON) tests/run.py $(SDK)
.PHONY: check
endif

.PHONY: all clean
.DELETE_ON_ERROR:
.DEFAULT_GOAL := all
