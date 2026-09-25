#include "platform.h"
#include "native.h"

uintptr_t game_platform_begin_frame(const struct osos_game_frame_input *input,
                                    struct game_framebuffer *framebuffer)
{
    const struct osos_surface *surface = osos_draw_surface();
    if (!surface || surface->color_mode != 5 || surface->width != 320 ||
        surface->height != 240 || !input->pixels_address ||
        surface->pixels_address != input->pixels_address)
        return 0;

    framebuffer->pixels = (uint16_t *)(uintptr_t)input->pixels_address;
    framebuffer->width = 320;
    framebuffer->height = 240;
    framebuffer->stride_pixels = 320;
    return (uintptr_t)surface;
}

void game_platform_present(uintptr_t surface)
{
    osos_present_surface(surface);
}
