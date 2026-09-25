#include "game_runtime.h"
#include "platform.h"

static int exit_requested;

void game_request_exit(void)
{
    exit_requested = 1;
}

void game_runtime_init(void)
{
    exit_requested = 0;
    if (game_application.init)
        game_application.init();
}

void game_runtime_shutdown(void)
{
    if (game_application.shutdown)
        game_application.shutdown();
    game_files_close_all();
}

void game_runtime_frame(const struct osos_game_frame_input *input,
                        struct osos_game_frame_output *output)
{
    if (!input || !output)
        return;

    /* osos sends states 4/5 while driving the shutdown transition. */
    if (exit_requested || input->state == 4 || input->state == 5) {
        output->state = OSOS_GAME_FRAME_DONE;
        return;
    }

    uint32_t address = input->events_address;
    while (address) {
        const struct osos_game_input_event *native =
            (const struct osos_game_input_event *)(uintptr_t)address;
        struct game_input_event event = {
            .button = native->button,
            .type = native->type,
            .timestamp_ms = native->value,
        };
        address = native->next_address;
        if (game_application.input)
            game_application.input(&event);
    }

    struct game_frame frame = {0};
    uintptr_t surface = game_platform_begin_frame(input, &frame.framebuffer);
    if (game_application.frame)
        game_application.frame(&frame);
    if (surface)
        game_platform_present(surface);
    if (exit_requested)
        output->state = OSOS_GAME_FRAME_DONE;
}
