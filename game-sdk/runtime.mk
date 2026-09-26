# SPDX-License-Identifier: GPL-3.0-only

# Set GAME_SDK before including this file. Outputs stay in the app's build tree.
GAME_SDK_BUILD ?= build/sdk
GAME_ARCH_FLAGS := -mcpu=arm926ej-s -marm -mfloat-abi=soft
CPPFLAGS += -I$(GAME_SDK)/include
CFLAGS += -std=c11 -Os $(GAME_ARCH_FLAGS) \
	-ffreestanding -fno-builtin -fno-common -fno-stack-protector \
	-fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables \
	-Wall -Wextra -ffunction-sections -fdata-sections
GAME_RUNTIME := $(GAME_SDK_BUILD)/libgame-runtime.a
GAME_STARTUP := $(GAME_SDK_BUILD)/startup.o
GAME_LINKER_SCRIPT := $(GAME_SDK)/runtime/game.lds
GAME_RUNTIME_OBJECTS := $(addprefix $(GAME_SDK_BUILD)/,\
	game_runtime.o memory.o time.o input.o files.o display.o)

$(GAME_RUNTIME): $(GAME_RUNTIME_OBJECTS)
	$(RM) $@
	$(AR) rcs $@ $^

$(GAME_SDK_BUILD)/%.o: $(GAME_SDK)/runtime/%.c Makefile $(GAME_SDK)/runtime.mk
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(GAME_STARTUP): $(GAME_SDK)/runtime/startup.S Makefile $(GAME_SDK)/runtime.mk
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(GAME_ARCH_FLAGS) -c $< -o $@

-include $(GAME_RUNTIME_OBJECTS:.o=.d)
