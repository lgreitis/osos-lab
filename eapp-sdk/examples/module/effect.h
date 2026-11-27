/* SPDX-License-Identifier: MIT */

/* The table each effect module begins with, and what the image shares with
 * the modules. */

#ifndef EFFECT_H
#define EFFECT_H

#include "eapp_draw.h"

struct effect {
    const char *name;
    void (*draw)(const struct eapp_framebuffer *fb);
};

/* Frames the image has drawn; the modules read it. */
extern uint32_t frames;

#endif
