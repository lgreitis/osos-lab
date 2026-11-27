/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/runtime/game_runtime.c (8fc840d) by lgreitis. */

#include "platform.h"
#include "runtime.h"

static int exit_requested;
static int started;
static uint8_t held;
static uint8_t wheel_touched;
static uint8_t framed; /* A frame has run since init. */
static uint32_t last_frame_ms;

void eapp_request_exit(void)
{
    exit_requested = 1;
}

void eapp_runtime_init(void)
{
    exit_requested = 0;
    started = 0;
    held = 0;
    wheel_touched = 0;
    framed = 0;
    eapp_clock_start();
    eapp_random_seed(eapp_clock_seed());
    if (eapp_application.init)
        eapp_application.init();
}

void eapp_runtime_shutdown(void)
{
    if (eapp_application.shutdown)
        eapp_application.shutdown();
    eapp_files_close_all();
    eapp_directories_close_all();
    eapp_saves_close_all();
    eapp_sounds_free_all();
}

static void deliver(const struct eapp_input_event *event)
{
    if (event->button < 8) {
        if (event->type == EAPP_INPUT_PRESS)
            held |= EAPP_HELD(event->button);
        else if (event->type == EAPP_INPUT_RELEASE)
            held &= (uint8_t)~EAPP_HELD(event->button);
    }
    if (eapp_application.input)
        eapp_application.input(event);
    if (!exit_requested && !eapp_application.handles_music_buttons)
        eapp_music_pass_button(event);
}

void eapp_runtime_input(const struct eapp_abi_input_event *native)
{
    struct eapp_input_event event = {
        .button = native->button,
        .type = native->type,
        .timestamp_ms = eapp_clock_stamp_ms(native->timestamp),
    };
    deliver(&event);
}

void eapp_runtime_frame(const struct eapp_abi_frame_input *input,
                        struct eapp_abi_frame_output *output)
{
    if (!input || !output)
        return;

    if (exit_requested || input->state == GameFrameState_ShuttingDown ||
        input->state == GameFrameState_ShutDown) {
        output->state = GameFrameState_Done;
        return;
    }

    /* The host made its GL context current between the init entry and the
     * first frame. */
    if (!started) {
        started = 1;
        if (eapp_application.start)
            eapp_application.start();
        if (exit_requested) {
            output->state = GameFrameState_Done;
            return;
        }
    }

    uint32_t address = input->events;
    while (address) {
        const struct eapp_abi_input_event *native =
            (const struct eapp_abi_input_event *)(uintptr_t)address;
        address = native->next;
        eapp_runtime_input(native);
        if (exit_requested) {
            output->state = GameFrameState_Done;
            return;
        }
    }

    struct eapp_frame frame = {0};
    frame.ticks_ms = eapp_ticks_ms();
    frame.delta_ms = framed ? frame.ticks_ms - last_frame_ms : 0;
    last_frame_ms = frame.ticks_ms;
    framed = 1;
    frame.wheel = eapp_input_read_wheel();
    if (frame.wheel.touched != wheel_touched) {
        wheel_touched = frame.wheel.touched;
        struct eapp_input_event touch = {
            .button = EAPP_BUTTON_WHEEL,
            .type = wheel_touched ? EAPP_INPUT_PRESS : EAPP_INPUT_RELEASE,
            .timestamp_ms = frame.ticks_ms,
        };
        deliver(&touch);
        if (exit_requested) {
            output->state = GameFrameState_Done;
            return;
        }
    }
    frame.held = held;
    void *surface = eapp_platform_begin_frame(input, &frame.framebuffer);
    if (eapp_application.frame)
        eapp_application.frame(&frame);
    if (surface)
        eapp_platform_present(surface);
    if (exit_requested)
        output->state = GameFrameState_Done;
}
