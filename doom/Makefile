# SPDX-License-Identifier: GPL-3.0-only

.DEFAULT_GOAL := all

CROSS_COMPILE ?= arm-elf-eabi-
CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar
OBJCOPY := $(CROSS_COMPILE)objcopy
PYTHON ?= python3
OUTPUT ?= build/doom.zip
GAME_SDK := ../game-sdk

include $(GAME_SDK)/runtime.mk

CPPFLAGS += -Iinclude
ENGINE := vendor/doomgeneric/doomgeneric
NEWLIB := build/newlib/arm-elf-eabi/newlib
DOOM_CPPFLAGS := -I$(ENGINE) -I$(NEWLIB)/targ-include \
	-Ivendor/newlib/newlib/libc/include -D_DEFAULT_SOURCE -DNORMALUNIX \
	-DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
ENGINE_NAMES := dummy am_map doomdef doomstat dstrings d_event d_items d_iwad \
	d_loop d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info \
	i_cdmus i_endoom i_joystick i_scale i_sound i_system i_timer memio m_argv \
	m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random p_ceilng \
	p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats \
	p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp \
	r_data r_draw r_main r_plane r_segs r_sky r_things sha1 sounds statdump \
	st_lib st_stuff s_sound tables v_video wi_stuff w_checksum w_file w_main \
	w_wad w_file_stdc i_input i_video doomgeneric
ENGINE_OBJECTS := $(addprefix build/engine/,$(addsuffix .o,$(ENGINE_NAMES)))
PORT_OBJECTS := build/port/doom_app.o build/port/console.o \
	build/port/newlib_glue.o build/port/zone_native.o

.PHONY: all package

all: package

package: build/doom.eapp build/Doom.raw.lcd5
	$(PYTHON) package.py --output "$(OUTPUT)"

build/Doom.raw.lcd5: assets/doom-icon.png artwork.py
	mkdir -p $(@D)
	$(PYTHON) artwork.py $< $@

build/doom.eapp: build/doom.elf
	$(OBJCOPY) -O binary $< $@

build/doom.elf: $(GAME_STARTUP) $(GAME_RUNTIME) $(PORT_OBJECTS) $(ENGINE_OBJECTS) \
		$(GAME_LINKER_SCRIPT) $(NEWLIB)/libc.a $(NEWLIB)/libm.a
	$(CC) $(GAME_ARCH_FLAGS) -nostdlib \
		-Wl,-T,$(GAME_LINKER_SCRIPT),-Map,build/doom.map,--no-undefined,--gc-sections \
		-Wl,--wrap=I_Quit,--wrap=G_BuildTiccmd \
		$(GAME_STARTUP) $(PORT_OBJECTS) $(ENGINE_OBJECTS) \
		-Wl,--start-group $(GAME_RUNTIME) $(NEWLIB)/libc.a \
		$(NEWLIB)/libm.a -lgcc -Wl,--end-group -o $@
	$(CROSS_COMPILE)size $@

build/engine/%.o: $(ENGINE)/%.c Makefile $(GAME_SDK)/runtime.mk
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DOOM_CPPFLAGS) $(CFLAGS) -Wno-unused-parameter -MMD -MP -c $< -o $@

build/port/%.o: src/%.c Makefile $(GAME_SDK)/runtime.mk
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DOOM_CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/port/newlib_glue.o: $(GAME_SDK)/runtime/newlib_glue.c Makefile $(GAME_SDK)/runtime.mk
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DOOM_CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(PORT_OBJECTS:.o=.d) $(ENGINE_OBJECTS:.o=.d)
