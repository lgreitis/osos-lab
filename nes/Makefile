# SPDX-License-Identifier: GPL-3.0-only
ID := reprise-nes
NAME := NES
ICON := assets/nes-icon.png
HEAP_KIB := 5120
ENGINE := vendor/infones/src
SOURCES := src/nes_app.c src/rom.c src/mappers.c \
    $(ENGINE)/InfoNES.cpp $(ENGINE)/InfoNES_pAPU.cpp $(ENGINE)/K6502.cpp
FILES := COPYING.INFONES COPYING.LUCIDE COPYING.EAPP-SDK COPYING.REPRISEOS
SDK ?= ../eapp-sdk
include $(SDK)/eapp.mk

# The port and the engine read char as signed, as InfoNES expects.
$(OUT)/app/%.o: EAPP_CFLAGS += -Isrc -I$(ENGINE) -fsigned-char
# InfoNES's C++ files compile as C at -O2. They and the InfoNES mappers that
# mappers.c includes raise warnings the port leaves unfixed; the -Wno flags
# keep them from failing the build.
$(OUT)/app/vendor/%.o: EAPP_CFLAGS += -x c -O2 -Wno-error \
    -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare
$(OUT)/app/src/mappers.c.o: EAPP_CFLAGS += -Wno-unused-parameter -Wno-unused-function

COPYING.INFONES: vendor/infones/LICENSE
	cp $< $@
COPYING.LUCIDE: assets/COPYING.LUCIDE
	cp $< $@
COPYING.EAPP-SDK: $(SDK)/LICENSE-MIT
	cp $< $@
COPYING.REPRISEOS: ../COPYING
	cp $< $@
