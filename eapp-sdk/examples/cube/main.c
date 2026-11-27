/* SPDX-License-Identifier: MIT */

/* A cube drawn with the host's OpenGL ES 1.1: six coloured faces with the
 * depth test on, turning by time and with the wheel, through the SDK's
 * perspective, which puts GL's up at the top of the screen; the version,
 * renderer and depth bits the host reports are drawn over the GL output with
 * the SDK's text after glFinish. Center pauses the turn, Menu exits. */

#include "eapp_draw.h"
#include "eapp_gl.h"

static const GLfloat vertices[24 * 3] = {
    /* Front, z = 1. */
    -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1,
    /* Back, z = -1. */
    1, -1, -1, -1, -1, -1, -1, 1, -1, 1, 1, -1,
    /* Left, x = -1. */
    -1, -1, -1, -1, -1, 1, -1, 1, 1, -1, 1, -1,
    /* Right, x = 1. */
    1, -1, 1, 1, -1, -1, 1, 1, -1, 1, 1, 1,
    /* Top, y = 1. */
    -1, 1, 1, 1, 1, 1, 1, 1, -1, -1, 1, -1,
    /* Bottom, y = -1. */
    -1, -1, -1, 1, -1, -1, 1, -1, 1, -1, -1, 1,
};

static const GLubyte colors[24 * 4] = {
    230, 60,  60,  255, 230, 60,  60,  255, 230, 60,  60,  255, 230, 60,  60,  255,
    60,  200, 80,  255, 60,  200, 80,  255, 60,  200, 80,  255, 60,  200, 80,  255,
    60,  110, 230, 255, 60,  110, 230, 255, 60,  110, 230, 255, 60,  110, 230, 255,
    240, 200, 40,  255, 240, 200, 40,  255, 240, 200, 40,  255, 240, 200, 40,  255,
    230, 230, 230, 255, 230, 230, 230, 255, 230, 230, 230, 255, 230, 230, 230, 255,
    200, 80,  200, 255, 200, 80,  200, 255, 200, 80,  200, 255, 200, 80,  200, 255,
};

/* Two counter-clockwise triangles per face. */
static const GLubyte indices[36] = {
    0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
    12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
};

static char version[32], renderer[32], depth[16];
static GLfloat angle, wheel_angle;
static int paused;

static void copy_text(char *out, size_t capacity, const GLubyte *text)
{
    eapp_format(out, capacity, "%s", text ? (const char *)text : "");
}

static void start(void)
{
    glViewport(0, 0, EAPP_SCREEN_WIDTH, EAPP_SCREEN_HEIGHT);
    eapp_gl_perspective(45.0f, 1.0f, 10.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);
    glClearColor(0.05f, 0.05f, 0.15f, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, vertices);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);

    copy_text(version, sizeof version, glGetString(GL_VERSION));
    copy_text(renderer, sizeof renderer, glGetString(GL_RENDERER));
    GLint bits = -1;
    glGetIntegerv(GL_DEPTH_BITS, &bits);
    eapp_format(depth, sizeof depth, "DEPTH %d", bits);
}

static void input(const struct eapp_input_event *event)
{
    if (event->type != EAPP_INPUT_PRESS)
        return;
    if (event->button == EAPP_BUTTON_MENU)
        eapp_request_exit();
    else if (event->button == EAPP_BUTTON_CENTER)
        paused = !paused;
}

static void frame(const struct eapp_frame *frame)
{
    if (!paused)
        angle += (GLfloat)frame->delta_ms * 0.05f;
    wheel_angle += (GLfloat)frame->wheel.delta * 4.0f;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -6.0f);
    glRotatef(wheel_angle, 0.0f, 1.0f, 0.0f);
    glRotatef(angle, 1.0f, 1.0f, 0.0f);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_BYTE, indices);
    glFinish();

    const struct eapp_framebuffer *fb = &frame->framebuffer;
    if (!fb->pixels)
        return;
    eapp_draw_text(fb, 8, 8, version, 0xffff, 1);
    eapp_draw_text(fb, 8, 20, renderer, 0xffff, 1);
    eapp_draw_text(fb, 8, 32, depth, 0xffff, 1);
    eapp_draw_text(fb, 8, 224, paused ? "PAUSED" : "CENTER PAUSES", 0xffff, 1);
}

const struct eapp eapp_application = {
    .start = start,
    .input = input,
    .frame = frame,
};
