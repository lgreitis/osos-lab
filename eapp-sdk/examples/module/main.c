/* SPDX-License-Identifier: MIT */

/* Runs one of two effects, each a module of the package: Center frees the
 * running one and loads the other. The screen shows the effect's name and
 * the heap address it was loaded at. Menu exits. */

#include "effect.h"

uint32_t frames;
static const char *const paths[] = {"stars.emod", "bars.emod"};
static const struct effect *effect;
static unsigned current;
static char status[40];

static void load(unsigned index)
{
    eapp_module_free(effect);
    current = index;
    effect = eapp_module_load(paths[index]);
    if (effect)
        eapp_format(status, sizeof status, "%s AT %x", effect->name,
                    (unsigned)(uintptr_t)effect);
    else
        eapp_format(status, sizeof status, "%s FAILED", paths[index]);
}

static void init(void)
{
    load(0);
}

static void input(const struct eapp_input_event *event)
{
    if (event->type != EAPP_INPUT_PRESS)
        return;
    if (event->button == EAPP_BUTTON_MENU)
        eapp_request_exit();
    else if (event->button == EAPP_BUTTON_CENTER)
        load(!current);
}

static void frame(const struct eapp_frame *frame)
{
    const struct eapp_framebuffer *fb = &frame->framebuffer;
    if (!fb->pixels)
        return;
    ++frames;
    if (effect)
        effect->draw(fb);
    else
        eapp_fill_rect(fb, 0, 0, EAPP_SCREEN_WIDTH, EAPP_SCREEN_HEIGHT, 0x0010);
    eapp_fill_rect(fb, 0, EAPP_SCREEN_HEIGHT - 20, EAPP_SCREEN_WIDTH, 20, 0x0000);
    eapp_draw_text(fb, 8, EAPP_SCREEN_HEIGHT - 16, status, 0xffff, 1);
}

static void shutdown(void)
{
    eapp_module_free(effect);
}

const struct eapp eapp_application = {
    .init = init, .input = input, .frame = frame, .shutdown = shutdown};
