/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef DOOM_PORT_H
#define DOOM_PORT_H

#include "eapp.h"

void doom_console_write(const char *text, size_t size);
void doom_console_draw(const struct eapp_framebuffer *fb);
_Noreturn void doom_exit(int status);

#endif
