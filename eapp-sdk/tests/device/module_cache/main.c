/* SPDX-License-Identifier: GPL-3.0-only */

/* Loads the two modules in turn, calls each and frees it, ROUNDS times a
 * frame until RUNS loads. A module whose code the caches still hold from the
 * other one returns the other's constant; the screen counts those, the loads
 * that reused the address just freed and the loads that failed. Menu exits. */

#include "eapp_draw.h"
#include "value.h"

enum { RUNS = 1000, ROUNDS = 20 };
static const char *const paths[] = {"one.emod", "two.emod"};
static const uint32_t expected[] = {1, 2};
static unsigned loads, wrong, reused, failed;
static uintptr_t last;

static void round_trip(unsigned index)
{
    const struct value *value = eapp_module_load(paths[index]);
    ++loads;
    if (!value) {
        ++failed;
        return;
    }
    reused += (uintptr_t)value == last;
    last = (uintptr_t)value;
    if (value->get() != expected[index])
        ++wrong;
    eapp_module_free(value);
}

static void input(const struct eapp_input_event *event)
{
    if (event->type == EAPP_INPUT_PRESS && event->button == EAPP_BUTTON_MENU)
        eapp_request_exit();
}

static void frame(const struct eapp_frame *frame)
{
    for (unsigned i = 0; i < ROUNDS && loads < RUNS; ++i)
        round_trip(loads % 2);
    const struct eapp_framebuffer *fb = &frame->framebuffer;
    if (!fb->pixels)
        return;
    char text[48];
    eapp_fill_rect(fb, 0, 0, EAPP_SCREEN_WIDTH, EAPP_SCREEN_HEIGHT, 0);
    eapp_format(text, sizeof text, "LOADS %u OF %u", loads, RUNS);
    eapp_draw_text(fb, 16, 40, text, 0xffff, 2);
    eapp_format(text, sizeof text, "WRONG %u", wrong);
    eapp_draw_text(fb, 16, 80, text, wrong ? 0xf800 : 0x07e0, 2);
    eapp_format(text, sizeof text, "SAME ADDRESS %u", reused);
    eapp_draw_text(fb, 16, 120, text, 0xffff, 2);
    eapp_format(text, sizeof text, "FAILED %u", failed);
    eapp_draw_text(fb, 16, 160, text, failed ? 0xf800 : 0xffff, 2);
}

const struct eapp eapp_application = {.input = input, .frame = frame};
