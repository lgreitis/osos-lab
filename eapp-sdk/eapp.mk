# SPDX-License-Identifier: GPL-3.0-only
# Adapted from RepriseOS game-sdk/runtime.mk and nes/Makefile (8fc840d) by lgreitis.
# Builds the eApp runtime for one app and packs the app into a package folder
# OUT/<ID>/ that goes to /Reprise/Games/<ID>/ or /Reprise/Apps/<ID>/.
#
#   ID := hello                  # the package id, its folder name
#   NAME := Hello                # what Extras lists
#   KIND := game                 # game (under /Reprise/Games/) or app (/Reprise/Apps/)
#   SOURCES := main.c            # the app's sources
#   ICON := icon.png             # 55 x 55 PNG shown in the list
#   HEAP_KIB := 5120             # the heap the host gives the app
#   VERSION := 1.0               # the package's version in its manifest
#   FILES := sounds/beep.wav     # data files packed under their relative paths
#   IMAGES := art/tiles.png      # PNGs packed as RGB565 pictures with a colour
#                                # key for transparent pixels; images.h names them
#   IMAGES_ALPHA := art/glow.png # PNGs packed as RGBA4444 texture data
#   IMAGE_PACKS := scene         # packs, each read in one go: scene.epak holds
#   scene_IMAGES := art/hero.png #   these as RGB565 pictures
#   scene_IMAGES_ALPHA := art/glow.png #   and these as RGBA4444
#   NAMES := names.toml          # the name per language: de_DE = "Hallo"
#   MODULES := physics           # code outside the image: physics.emod, which
#   physics_SOURCES := physics.c #   eapp_module_load reads into the heap
#   THUMB := yes                 # the app's and modules' sources as Thumb code
#   ARM_SOURCES := blit.c        #   except these, and <module>_ARM_SOURCES
#   EXTRA_CFLAGS := -DFAST       # added to the compile flags
#   EXTRA_LDFLAGS := -Wl,--print-memory-usage  # added to the link flags
#   LDLIBS := -lm                # libraries linked before the runtime
#   EAPP_WERROR :=               # -Werror by default; empty leaves warnings as warnings
#   include $(SDK)/eapp.mk
#
# A source may be generated: name it in SOURCES with its path (gen/tiles.c)
# and give it a rule; the object rule takes any relative path.
#
# An app with its own link step (newlib, extra libraries) leaves SOURCES
# empty and writes the rule for $(OUT)/$(ID).elf itself, with EAPP_ARCH_FLAGS,
# EAPP_CFLAGS, EAPP_LDFLAGS, EAPP_STARTUP and EAPP_RUNTIME, the targets
# $(EAPP_STARTUP) and $(EAPP_RUNTIME), and with modules EAPP_MODULE_LDFLAGS
# and $(OUT)/modules.ld (docs/build.md); the rules below pack its image.

SDK ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
CROSS_COMPILE ?= arm-elf-eabi-
OUT ?= build
KIND ?= game
HEAP_KIB ?= 5120
VERSION ?= 1.0
EAPP_WERROR ?= -Werror
# The symbols header copied from the Revola reversing repository: the host interface
# ids, types and image limits.
EAPP_SYMBOLS := $(SDK)/firmware/osos-2.0.5.h
EAPP_HEADER_BYTES := $(shell grep -E '^\#define GameImageHeader_bytes ' $(EAPP_SYMBOLS) | cut -d' ' -f3)

ifeq ($(filter game app,$(KIND)),)
$(error KIND must be game or app)
endif
$(if $(ID),,$(error ID names the package folder))
$(if $(NAME),,$(error NAME is what Extras lists))
$(if $(ICON),,$(error ICON names a 55 x 55 PNG))

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar
OBJCOPY := $(CROSS_COMPILE)objcopy
EAPP_ARCH_FLAGS := -mcpu=arm926ej-s -marm -mfloat-abi=soft
# Apple's code is 256 MiB away from the image, out of reach of a plain BL.
EAPP_CFLAGS := -std=c11 -Os $(EAPP_ARCH_FLAGS) -mlong-calls \
    -ffreestanding -fno-builtin -fno-common -fno-stack-protector \
    -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -Wall -Wextra $(EAPP_WERROR) -ffunction-sections -fdata-sections \
    -I$(SDK) -I$(SDK)/include -I$(SDK)/firmware -I$(OUT) $(EXTRA_CFLAGS)
EAPP_BUILD_ID := -Wl,--build-id=md5
EAPP_LDFLAGS := -nostdlib -Wl,-T,$(SDK)/runtime/eapp.lds -Wl,--gc-sections \
    -Wl,--defsym,eapp_header_bytes=$(EAPP_HEADER_BYTES) \
    $(if $(strip $(MODULES)),$(EAPP_BUILD_ID))
# A module links at 0 against the image's symbols and keeps its relocations
# for eapp-pack. It links its own libgcc, named before the image, so it never
# depends on the helpers the image happens to hold; Thumb code calls some of
# them with short calls. The linker's veneers carry no relocation, so they
# must be position-independent.
EAPP_MODULE_LDFLAGS := -nostdlib -Wl,-T,$(SDK)/runtime/module.lds -Wl,--gc-sections \
    -Wl,--emit-relocs -Wl,--pic-veneer
EAPP_LIBGCC = $(shell $(CC) $(EAPP_ARCH_FLAGS) -print-libgcc-file-name)
# Thumb apps build the runtime apart, without tail calls (below), so turning
# THUMB on never reuses objects built without it.
EAPP_RUNTIME_DIR := $(OUT)/runtime$(if $(strip $(THUMB)),-thumb)
EAPP_RUNTIME := $(EAPP_RUNTIME_DIR)/libeapp.a
# What every image begins with: the eApp header and the entry code, linked
# as objects so the archive's pull rules do not decide about them.
EAPP_STARTUP := $(EAPP_RUNTIME_DIR)/header.o $(EAPP_RUNTIME_DIR)/startup.o
EAPP_RUNTIME_OBJECTS := $(addprefix $(EAPP_RUNTIME_DIR)/,\
    runtime.o imports.o request.o memory.o time.o input.o files.o directory.o save.o display.o \
    draw.o ui.o wheel.o random.o sound.o music.o system.o gl.o text.o users.o media.o media_image.o \
    format.o math.o bitmap.o pack.o module.o cache.o)

$(EAPP_RUNTIME): $(EAPP_RUNTIME_OBJECTS)
	$(RM) $@
	$(AR) rcs $@ $^

# A few of Apple's routines return with mov pc, lr, which resumes a Thumb
# caller in ARM state; with THUMB set the runtime calls the host and returns
# itself instead of tail-calling, so a host return lands in ARM code.
EAPP_RUNTIME_CFLAGS = $(EAPP_CFLAGS) $(if $(strip $(THUMB)),-fno-optimize-sibling-calls)

$(EAPP_RUNTIME_DIR)/%.o: $(SDK)/runtime/%.c $(SDK)/eapp.mk
	@mkdir -p $(@D)
	$(CC) $(EAPP_RUNTIME_CFLAGS) -MMD -MP -c $< -o $@

# The interface ids and counts the import list names, in a form the assembler reads.
$(EAPP_RUNTIME_DIR)/imports_ids.h: $(EAPP_SYMBOLS) $(SDK)/eapp.mk
	@mkdir -p $(@D)
	grep -E '^#define (GameAPI_[A-Za-z]+_(Id[0-3]|Count)|GameAPIModule_NameBytes|GameImport_Tail) ' $< > $@

$(EAPP_RUNTIME_DIR)/imports.o: $(SDK)/runtime/imports.S $(SDK)/runtime/imports.def \
		$(EAPP_RUNTIME_DIR)/imports_ids.h $(SDK)/eapp.mk
	@mkdir -p $(@D)
	$(CC) $(EAPP_ARCH_FLAGS) -I$(@D) -I$(SDK)/runtime -c $< -o $@

$(EAPP_RUNTIME_DIR)/startup.o $(EAPP_RUNTIME_DIR)/cache.o: $(EAPP_RUNTIME_DIR)/%.o: \
		$(SDK)/runtime/%.S $(SDK)/eapp.mk
	@mkdir -p $(@D)
	$(CC) $(EAPP_ARCH_FLAGS) -c $< -o $@

-include $(EAPP_RUNTIME_OBJECTS:.o=.d) $(EAPP_RUNTIME_DIR)/header.d

# -mthumb for a source of the app or a module when THUMB is set and the
# source is not listed in $(2); the runtime stays ARM.
EAPP_MODE = $(if $(and $(strip $(THUMB)),$(filter-out $(2),$(1))),-mthumb)

# The default rules for an app of plain C sources without a C library.
EAPP_OBJECTS := $(patsubst %,$(OUT)/app/%.o,$(SOURCES))
EAPP_PICTURES := $(foreach i,$(IMAGES),--picture 565 $(i)) \
    $(foreach i,$(IMAGES_ALPHA),--picture 4444 $(i)) \
    $(foreach p,$(IMAGE_PACKS),--pack $(p) $(foreach i,$($(p)_IMAGES),565 $(i)) \
        $(foreach i,$($(p)_IMAGES_ALPHA),4444 $(i)))
EAPP_PICTURE_FILES := $(IMAGES) $(IMAGES_ALPHA) \
    $(foreach p,$(IMAGE_PACKS),$($(p)_IMAGES) $($(p)_IMAGES_ALPHA))

# The pictures' names and sizes, for #include "images.h"; the app's objects
# wait for it when there are pictures.
$(OUT)/images.h: $(EAPP_PICTURE_FILES) $(SDK)/eapp.mk
	@mkdir -p $(@D)
	python3 $(SDK)/eapp-pack --images-header $@ --header-only $(EAPP_PICTURES)

$(OUT)/app/%.o: % $(if $(strip $(EAPP_PICTURE_FILES)),$(OUT)/images.h)
	@mkdir -p $(dir $@)
	$(CC) $(EAPP_CFLAGS) $(call EAPP_MODE,$<,$(ARM_SOURCES)) -MMD -MP -c $< -o $@
-include $(EAPP_OBJECTS:.o=.d)

# Modules: objects under OUT/modules/<name>/, linked into OUT/modules/<name>.elf.
# OUT/modules.ld keeps in the image what their objects use of it.
EAPP_MODULE_OBJECTS = $(patsubst %,$(OUT)/modules/$(1)/%.o,$($(1)_SOURCES))
EAPP_ALL_MODULE_OBJECTS := $(foreach m,$(MODULES),$(call EAPP_MODULE_OBJECTS,$(m)))
EAPP_MODULE_ELFS := $(patsubst %,$(OUT)/modules/%.elf,$(MODULES))
EAPP_MODULES_SCRIPT := $(if $(strip $(MODULES)),$(OUT)/modules.ld)

define EAPP_MODULE_RULES
$(OUT)/modules/$(1)/%.o: % $(if $(strip $(EAPP_PICTURE_FILES)),$(OUT)/images.h)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(EAPP_CFLAGS) $$(call EAPP_MODE,$$<,$$($(1)_ARM_SOURCES)) -MMD -MP -c $$< -o $$@

$(OUT)/modules/$(1).elf: $(call EAPP_MODULE_OBJECTS,$(1)) $(OUT)/$(ID).elf $(SDK)/runtime/module.lds
	$$(CC) $$(EAPP_ARCH_FLAGS) $(call EAPP_MODULE_OBJECTS,$(1)) -lgcc $$(EAPP_MODULE_LDFLAGS) \
	    -Wl,--just-symbols=$(OUT)/$(ID).elf -Wl,-Map,$$(@:.elf=.map) -o $$@
endef
$(foreach m,$(MODULES),$(eval $(call EAPP_MODULE_RULES,$(m))))
-include $(EAPP_ALL_MODULE_OBJECTS:.o=.d)

# Rewritten only when its contents change, so the image relinks only then;
# the stamp records that the objects were read, and the app's Makefile names
# them.
EAPP_MODULE_EXTERNS = python3 $(SDK)/eapp-pack --prefix $(CROSS_COMPILE) \
    --libgcc $(EAPP_LIBGCC) --module-externs $(OUT)/modules.ld $(EAPP_ALL_MODULE_OBJECTS)
$(OUT)/modules.ld: $(OUT)/modules.stamp
	@test -f $@ || $(EAPP_MODULE_EXTERNS)
$(OUT)/modules.stamp: $(EAPP_ALL_MODULE_OBJECTS) $(firstword $(MAKEFILE_LIST)) \
		$(SDK)/eapp.mk $(SDK)/eapp_package.py
	$(EAPP_MODULE_EXTERNS)
	touch $@

# The link rule for an app of plain C sources; an app without SOURCES writes
# its own.
ifneq ($(strip $(SOURCES)),)
$(OUT)/$(ID).elf: $(EAPP_STARTUP) $(EAPP_OBJECTS) $(EAPP_RUNTIME) $(SDK)/runtime/eapp.lds \
		$(EAPP_MODULES_SCRIPT)
	$(CC) $(EAPP_ARCH_FLAGS) $(EAPP_STARTUP) $(EAPP_OBJECTS) $(EAPP_MODULES_SCRIPT) $(LDLIBS) \
	    $(EAPP_RUNTIME) -lgcc $(EAPP_LDFLAGS) $(EXTRA_LDFLAGS) -Wl,-Map,$(@:.elf=.map) -o $@
endif

$(OUT)/$(ID).eapp: $(OUT)/$(ID).elf
	$(OBJCOPY) -O binary $< $@

all: $(OUT)/$(ID)/Manifest.plist

$(OUT)/$(ID)/Manifest.plist: $(OUT)/$(ID).eapp $(ICON) $(FILES) $(NAMES) $(EAPP_PICTURE_FILES) \
		$(EAPP_MODULE_ELFS)
	python3 $(SDK)/eapp-pack --kind $(KIND) --id $(ID) --name "$(NAME)" \
	    $(if $(NAMES),--names $(NAMES)) --version "$(VERSION)" \
	    --image $< --icon $(ICON) --symbols $(EAPP_SYMBOLS) \
	    --heap-kib $(HEAP_KIB) --out $(OUT) $(EAPP_PICTURES) \
	    $(if $(strip $(MODULES)),--elf $(OUT)/$(ID).elf --prefix $(CROSS_COMPILE) \
	        $(foreach m,$(MODULES),--module $(m) $(OUT)/modules/$(m).elf)) -- $(FILES)

clean:
	rm -rf $(OUT)

.PHONY: all clean
.DEFAULT_GOAL := all
