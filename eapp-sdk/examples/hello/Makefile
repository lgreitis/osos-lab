# SPDX-License-Identifier: MIT
ID := hello
NAME := Hello
KIND ?= game
SOURCES := main.c
ICON := icon.png
FILES := beep.wav
IMAGES := art/badge.png
NAMES := names.toml
SDK ?= ../..
include $(SDK)/eapp.mk

beep.wav: tone.py
	python3 $< $@

clean: clean-files
clean-files:
	rm -f $(FILES)
.PHONY: clean-files
