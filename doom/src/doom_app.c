/* SPDX-License-Identifier: GPL-3.0-only */

#include "doom_port.h"
#include "game_libc.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_system.h"
#include "m_controls.h"
#include "doomstat.h"
#include "g_game.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#define KEY_QUEUE_SIZE 128

enum { MENU, PLAY, NEXT, PREVIOUS, CENTER, BUTTONS };

static const uint8_t button_ids[BUTTONS] = {
    GAME_BUTTON_MENU,     GAME_BUTTON_PLAY,   GAME_BUTTON_NEXT,
    GAME_BUTTON_PREVIOUS, GAME_BUTTON_CENTER,
};

static uint8_t held[BUTTONS], active_keys[BUTTONS][2];
static int started, failed, error_visible;
static uint32_t chord_start;
static int chord_active;
static int hold_known, hold_locked;
static jmp_buf exit_context;
static const struct game_framebuffer *framebuffer;

static struct {
    uint8_t key, pressed;
} keys[KEY_QUEUE_SIZE];

static unsigned key_read, key_write;

extern int messageToPrint;

static void push_key(uint8_t key, int pressed)
{
    if (!key)
        return;

    unsigned next = (key_write + 1) % KEY_QUEUE_SIZE;
    if (next == key_read)
        return;

    keys[key_write].key = key;
    keys[key_write].pressed = !!pressed;
    key_write = next;
}

static void input(const struct game_input_event *event)
{
    if (event->type != GAME_INPUT_RELEASE && event->type != GAME_INPUT_PRESS)
        return;

    if (failed) {
        if (error_visible)
            game_request_exit();
        return;
    }
    if (!started || hold_locked)
        return;

    unsigned button;
    for (button = 0; button < BUTTONS; ++button)
        if (button_ids[button] == event->button)
            break;
    if (button == BUTTONS)
        return;

    int pressed = event->type == GAME_INPUT_PRESS;
    if (held[button] == pressed)
        return;
    held[button] = pressed;

    if (!pressed) {
        for (unsigned i = 0; i < 2; ++i) {
            push_key(active_keys[button][i], 0);
            active_keys[button][i] = 0;
        }
        return;
    }

    uint8_t key;
    if (menuactive || messageToPrint) {
        const uint8_t mapping[] = {KEY_UPARROW, KEY_DOWNARROW, KEY_RIGHTARROW,
                                   KEY_LEFTARROW, KEY_ENTER};
        key = button == CENTER && messageToPrint ? 'y' : mapping[button];
    } else {
        const uint8_t mapping[] = {KEY_UPARROW, KEY_FIRE, KEY_RIGHTARROW, KEY_LEFTARROW,
                                   '='};
        key = mapping[button];
        if (button == MENU) {
            active_keys[button][1] = KEY_USE;
            push_key(KEY_USE, 1);
        }
    }

    active_keys[button][0] = key;
    push_key(key, 1);
}

static void release_inputs(void)
{
    const uint8_t bindings[] = {
        KEY_UPARROW, KEY_DOWNARROW, KEY_LEFTARROW, KEY_RIGHTARROW, KEY_FIRE, KEY_USE,
        '=',         KEY_ENTER,     'y',
    };

    key_read = key_write = 0;
    memset(held, 0, sizeof(held));
    memset(active_keys, 0, sizeof(active_keys));
    chord_active = 0;

    /* Menu handling can consume key-up events before they reach gameplay. */
    for (unsigned i = 0; i < sizeof(bindings); ++i) {
        event_t event = {.type = ev_keyup, .data1 = bindings[i]};
        G_Responder(&event);
    }
}

static void poll_hold(void)
{
    int locked = game_hold_locked();
    if (locked >= 0) {
        int changed = hold_known && locked != hold_locked;
        hold_known = 1;
        hold_locked = locked;
        if (changed && locked && started) {
            release_inputs();
            push_key(KEY_ESCAPE, 1);
            push_key(KEY_ESCAPE, 0);
        }
    }
}

void __real_G_BuildTiccmd(ticcmd_t *cmd, int maketic);

void __wrap_G_BuildTiccmd(ticcmd_t *cmd, int maketic)
{
    __real_G_BuildTiccmd(cmd, maketic);

    struct game_wheel wheel = game_read_wheel();
    if (hold_locked) {
        cmd->forwardmove = cmd->sidemove = cmd->angleturn = 0;
        cmd->buttons = 0;
        return;
    }
    if (menuactive || messageToPrint || paused || gamestate != GS_LEVEL)
        return;

    /* Rockbox uses the sign of each scroll event and saturates at +/-50. */
    int side = cmd->sidemove;
    if (wheel.delta > 0)
        side += 50;
    if (wheel.delta < 0)
        side -= 50;
    if (side > 50)
        side = 50;
    if (side < -50)
        side = -50;
    cmd->sidemove = side;
}

static void frame(const struct game_frame *frame)
{
    framebuffer = &frame->framebuffer;
    if (!framebuffer->pixels)
        return;

    if (failed) {
        doom_console_draw(framebuffer);
        error_visible = 1;
        return;
    }
    poll_hold();

    uint32_t now = game_ticks_ms();
    if (held[PREVIOUS] && held[NEXT]) {
        if (!chord_active) {
            chord_active = 1;
            chord_start = now;
        }
        if (now - chord_start >= 1500) {
            game_request_exit();
            return;
        }
    } else {
        chord_active = 0;
    }

    int result = setjmp(exit_context);
    if (result) {
        if (result == 1)
            game_request_exit();
        else {
            failed = 1;
            doom_console_draw(framebuffer);
            error_visible = 1;
        }
        return;
    }

    if (!started) {
        static char *argv[] = {"doom",     "-iwad",    "doom1.wad", "-nogui",
                               "-nosound", "-gfxmode", "rgb565",    NULL};

        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        game_read_wheel();
        started = 1;
        doomgeneric_Create(sizeof(argv) / sizeof(argv[0]) - 1, argv);

        key_menu_incscreen = 0;
        key_menu_decscreen = 0;
        key_nextweapon = '=';
    } else {
        doomgeneric_Tick();
    }

    framebuffer = NULL;
}

const struct game_app game_application = {
    .package_id = "reprise-doom",
    .input = input,
    .frame = frame,
};

void DG_Init(void)
{
    if (!DG_ScreenBuffer)
        I_Error("No memory for screen buffer");
}

void DG_DrawFrame(void)
{
    if (!framebuffer || !framebuffer->pixels)
        return;

    const uint16_t *source = (const uint16_t *)DG_ScreenBuffer;
    for (unsigned y = 0; y < 240; ++y)
        memcpy(framebuffer->pixels + y * framebuffer->stride_pixels,
               source + (y * 5 / 6) * 320, 320 * sizeof(uint16_t));
}

void DG_SleepMs(uint32_t ms)
{
    game_sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void)
{
    return game_ticks_ms();
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    if (key_read == key_write)
        return 0;

    *pressed = keys[key_read].pressed;
    *key = keys[key_read].key;
    key_read = (key_read + 1) % KEY_QUEUE_SIZE;
    return 1;
}

_Noreturn void doom_exit(int status)
{
    longjmp(exit_context, status ? 2 : 1);
}

void game_libc_write(const char *text, size_t size)
{
    doom_console_write(text, size);
}

_Noreturn void game_libc_exit(int status)
{
    doom_exit(status);
}

void __wrap_I_Quit(void)
{
    extern void __real_I_Quit(void);
    __real_I_Quit();
    doom_exit(0);
}
