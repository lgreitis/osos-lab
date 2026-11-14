# SPDX-License-Identifier: GPL-3.0-only
.DEFAULT_GOAL := all
CROSS_COMPILE ?= arm-elf-eabi-
CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar
OBJCOPY := $(CROSS_COMPILE)objcopy
PYTHON ?= python3
OUTPUT ?= build/nes.zip
GAME_SDK := ../game-sdk
include $(GAME_SDK)/runtime.mk

ENGINE := vendor/infones/src
CPPFLAGS += -I$(ENGINE) -Isrc
CFLAGS += -fsigned-char
ENGINE_NAMES := InfoNES InfoNES_pAPU K6502
ENGINE_OBJECTS := $(addprefix build/engine/,$(addsuffix .o,$(ENGINE_NAMES)))
PORT_OBJECTS := build/port/nes_app.o build/port/rom.o build/port/mappers.o

.PHONY: all package
all: package
package: build/nes.eapp
	$(PYTHON) package.py --output "$(OUTPUT)"

build/nes.eapp: build/nes.elf
	$(OBJCOPY) -O binary $< $@

build/nes.elf: $(GAME_STARTUP) $(GAME_RUNTIME) $(PORT_OBJECTS) $(ENGINE_OBJECTS) $(GAME_LINKER_SCRIPT)
	$(CC) $(GAME_ARCH_FLAGS) -nostdlib -Wl,-T,$(GAME_LINKER_SCRIPT),-Map,build/nes.map,--no-undefined,--gc-sections \
		$(GAME_STARTUP) $(PORT_OBJECTS) $(ENGINE_OBJECTS) $(GAME_RUNTIME) -lgcc -o $@
	$(CROSS_COMPILE)size $@

build/engine/%.o: $(ENGINE)/%.cpp Makefile
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -O2 -x c -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare -MMD -MP -c $< -o $@

build/port/%.o: src/%.c Makefile
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(PORT_OBJECTS:.o=.d) $(ENGINE_OBJECTS:.o=.d)
