# SPDX-License-Identifier: GPL-3.0-only
ID := reprise-doom
NAME := Doom
ICON := assets/doom-icon.png
HEAP_KIB := 5120
# The shareware episode when setup.py --shareware fetched it, and the notices
# of what the image links.
FILES := $(wildcard doom1.wad) COPYING.DOOM COPYING.NEWLIB COPYING.EAPP-SDK \
    COPYING.REPRISEOS
SDK ?= ../eapp-sdk
include $(SDK)/eapp.mk

ENGINE := vendor/doomgeneric/doomgeneric
NEWLIB := vendor/newlib-build/arm-elf-eabi/newlib
DOOM_CFLAGS := $(EAPP_CFLAGS) -Iinclude -I$(ENGINE) -I$(NEWLIB)/targ-include \
    -Ivendor/newlib/newlib/libc/include -D_DEFAULT_SOURCE -DNORMALUNIX \
    -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -DFEATURE_SOUND
ENGINE_NAMES := dummy am_map doomdef doomstat dstrings d_event d_items d_iwad \
    d_loop d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info \
    i_cdmus i_endoom i_joystick i_scale i_sound i_system i_timer memio m_argv \
    m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random p_ceilng \
    p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats \
    p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp \
    r_data r_draw r_main r_plane r_segs r_sky r_things sha1 sounds statdump \
    st_lib st_stuff s_sound tables v_video wi_stuff w_checksum w_file w_main \
    w_wad w_file_stdc i_input i_video doomgeneric
ENGINE_OBJECTS := $(addprefix $(OUT)/engine/,$(addsuffix .o,$(ENGINE_NAMES)))
PORT_OBJECTS := $(addprefix $(OUT)/port/,doom_app.o console.o sound.o zone_native.o \
    newlib_glue.o)

# The runtime comes before newlib in the group so its memcpy, memmove, memset
# and memcmp are the ones linked; --wrap hooks the quit and the tic builder.
$(OUT)/$(ID).elf: $(EAPP_STARTUP) $(PORT_OBJECTS) $(ENGINE_OBJECTS) $(EAPP_RUNTIME) \
		$(SDK)/runtime/eapp.lds $(NEWLIB)/libc.a $(NEWLIB)/libm.a
	$(CC) $(EAPP_ARCH_FLAGS) $(EAPP_STARTUP) $(PORT_OBJECTS) $(ENGINE_OBJECTS) \
	    -Wl,--start-group $(EAPP_RUNTIME) $(NEWLIB)/libc.a $(NEWLIB)/libm.a -lgcc -Wl,--end-group \
	    $(EAPP_LDFLAGS) -Wl,--wrap=I_Quit,--wrap=G_BuildTiccmd,--no-undefined \
	    -Wl,-Map,$(@:.elf=.map) -o $@
	$(CROSS_COMPILE)size $@

# DoomGeneric's sources raise warnings the port leaves unfixed; -Wno-error
# keeps them from failing the build.
$(OUT)/engine/%.o: $(ENGINE)/%.c Makefile $(SDK)/eapp.mk
	@mkdir -p $(@D)
	$(CC) $(DOOM_CFLAGS) -Wno-error -Wno-unused-parameter -MMD -MP -c $< -o $@
$(OUT)/port/%.o: src/%.c Makefile $(SDK)/eapp.mk
	@mkdir -p $(@D)
	$(CC) $(DOOM_CFLAGS) -MMD -MP -c $< -o $@
$(OUT)/port/newlib_glue.o: $(SDK)/runtime/newlib_glue.c Makefile $(SDK)/eapp.mk
	@mkdir -p $(@D)
	$(CC) $(DOOM_CFLAGS) -MMD -MP -c $< -o $@
-include $(PORT_OBJECTS:.o=.d) $(ENGINE_OBJECTS:.o=.d)

COPYING.DOOM: vendor/doomgeneric/LICENSE
	cp $< $@
COPYING.NEWLIB: vendor/newlib/COPYING.NEWLIB
	cp $< $@
COPYING.EAPP-SDK: $(SDK)/LICENSE-MIT
	cp $< $@
COPYING.REPRISEOS: ../COPYING
	cp $< $@
