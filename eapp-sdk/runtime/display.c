/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/runtime/display.c (8fc840d) by lgreitis. */

#include "osos-2.0.5.h"
#include "platform.h"
#include "eapp_abi.h"

#define FIELD(base, offset, type) (*(const type *)((const uint8_t *)(base) + (offset)))

void *eapp_platform_begin_frame(const struct eapp_abi_frame_input *input,
                                struct eapp_framebuffer *framebuffer)
{
    void *surface = eglGetCurrentSurface(GameSurface_Draw);
    if (!surface ||
        FIELD(surface, GameSurface_color_mode, uint8_t) != GameSurfaceColorMode_RGB565 ||
        FIELD(surface, GameSurface_width, uint32_t) != EAPP_SCREEN_WIDTH ||
        FIELD(surface, GameSurface_height, uint32_t) != EAPP_SCREEN_HEIGHT || !input->pixels ||
        FIELD(surface, GameSurface_pixels, uint32_t) != input->pixels)
        return NULL;

    framebuffer->pixels = (uint16_t *)(uintptr_t)input->pixels;
    framebuffer->width = EAPP_SCREEN_WIDTH;
    framebuffer->height = EAPP_SCREEN_HEIGHT;
    framebuffer->stride_pixels = EAPP_SCREEN_WIDTH;
    return surface;
}

void eapp_platform_present(void *surface)
{
    eglSwapBuffers(NULL, surface);
}
