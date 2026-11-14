/* SPDX-License-Identifier: GPL-3.0-only */
#include "game_api.h"
#include "game_ui.h"
#include <stdbool.h>
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "rom.h"

_Static_assert(sizeof(DWORD) == 4, "InfoNES requires 32-bit DWORD");
_Static_assert(sizeof(NesHeader) == 16, "iNES header size");

static const uint16_t palette555[64] = {
    0x39ce, 0x1071, 0x0015, 0x2013, 0x440e, 0x5402, 0x5000, 0x3c20, 0x20a0, 0x0100,
    0x0140, 0x00e2, 0x0ceb, 0,      0,      0,      0x5ef7, 0x01dd, 0x10fd, 0x401e,
    0x5c17, 0x700b, 0x6ca0, 0x6521, 0x45c0, 0x0240, 0x02a0, 0x0247, 0x0211, 0,
    0,      0,      0x7fff, 0x1eff, 0x2e5f, 0x223f, 0x79ff, 0x7dd6, 0x7dcc, 0x7e67,
    0x7ae7, 0x4342, 0x2769, 0x2ff3, 0x03bb, 0,      0,      0,      0x7fff, 0x579f,
    0x635f, 0x6b3f, 0x7f1f, 0x7f1b, 0x7ef6, 0x7f75, 0x7f94, 0x73f4, 0x57d7, 0x5bf9,
    0x4ffe, 0,      0,      0,
};
WORD NesPalette[64];

static struct game_file_picker picker;
static const struct game_menu_item regions[] = {{"PAL (EUROPE) - 50 HZ", NULL},
                                                {"NTSC (USA/JAPAN) - 60 HZ", NULL}};
static struct game_menu region_menu = {"VIDEO SYSTEM", regions, 2, 0};

enum nes_button {
    NES_A = 0x01,
    NES_B = 0x02,
    NES_START = 0x08,
    NES_UP = 0x10,
    NES_DOWN = 0x20,
    NES_LEFT = 0x40,
    NES_RIGHT = 0x80,
};

enum pause_action { RESUME, START, MENU_FUNCTION, GAME_LIST, QUIT };
static struct game_menu_item pause_items[] = {
    {"RESUME", NULL},    {"START BUTTON", NULL}, {"MENU BUTTON: B", NULL},
    {"GAME LIST", NULL}, {"QUIT", NULL},
};
static struct game_menu pause_menu = {"PAUSED", pause_items, 5, RESUME};
static bool held[6], running, paused, locked, choosing_region, menu_is_up;
static uint32_t last_frame_ms, frame_credit;
static uint8_t pending_buttons;

static uint8_t button_mask(unsigned button)
{
    static const uint8_t buttons[6] = {0, NES_B, NES_A, NES_RIGHT, NES_LEFT, NES_DOWN};
    return button == GAME_BUTTON_MENU && menu_is_up ? NES_UP : buttons[button];
}

static void clear_input(void)
{
    for (unsigned i = 0; i < 6; ++i)
        held[i] = false;
    pending_buttons = 0;
    PAD1_Latch = PAD2_Latch = 0;
    game_read_wheel();
}

static void resume_game(void)
{
    paused = false;
    last_frame_ms = game_ticks_ms();
    frame_credit = 1000;
    clear_input();
}

static void pause_input(const struct game_input_event *event)
{
    enum game_menu_action action = game_menu_input(&pause_menu, event);
    if (action == GAME_MENU_BACK) {
        resume_game();
        return;
    }
    if (action != GAME_MENU_SELECT)
        return;
    switch (pause_menu.selected) {
    case RESUME:
    case START:
        resume_game();
        if (pause_menu.selected == START)
            pending_buttons = NES_START;
        break;
    case MENU_FUNCTION:
        menu_is_up = !menu_is_up;
        pause_items[MENU_FUNCTION].label =
            menu_is_up ? "MENU BUTTON: UP" : "MENU BUTTON: B";
        break;
    case GAME_LIST:
        InfoNES_ReleaseRom();
        running = paused = false;
        clear_input();
        break;
    case QUIT:
        game_request_exit();
        break;
    }
}

static void init(void)
{
    for (unsigned i = 0; i < 64; ++i) {
        unsigned c = palette555[i];
        NesPalette[i] = ((c & 0x7fe0) << 1) | ((c >> 4) & 0x20) | (c & 31);
    }
    InfoNES_Init();
    int result = game_file_picker_open(&picker, "NES", "", ".nes");
    if (result < 0)
        nes_error =
            result == -2 ? "TOO MANY ROMS (MAX 64)" : "CANNOT READ ROM DIRECTORY";
}

static void start_game(void)
{
    if (!picker.menu.count)
        return;
    clear_input();
    nes_error = NULL;
    running = InfoNES_Load(picker.menu.items[picker.menu.selected].value) == 0;
    if (!running) {
        InfoNES_ReleaseRom();
        if (!nes_error)
            nes_error = "UNSUPPORTED ROM";
        return;
    }
    APU_Mute = 1;
    paused = false;
    choosing_region = true;
    if (NesHeader.byReserve[1] & 1)
        region_menu.selected = 0;
}

static void input(const struct game_input_event *event)
{
    if (event->button < 1 || event->button > 5 ||
        (event->type != GAME_INPUT_PRESS && event->type != GAME_INPUT_RELEASE))
        return;
    bool pressed = event->type == GAME_INPUT_PRESS;
    bool was_held = held[event->button];
    held[event->button] = pressed && !locked;
    if (!pressed || was_held || locked)
        return;
    if (nes_error) {
        nes_error = NULL;
        return;
    }
    if (choosing_region) {
        enum game_menu_action action = game_menu_input(&region_menu, event);
        if (action == GAME_MENU_SELECT) {
            InfoNES_Pal = region_menu.selected == 0;
            choosing_region = false;
            resume_game();
        } else if (action == GAME_MENU_BACK) {
            InfoNES_ReleaseRom();
            running = choosing_region = false;
            clear_input();
        }
        return;
    }
    if (!running) {
        enum game_menu_action action = game_menu_input(&picker.menu, event);
        if (action == GAME_MENU_BACK)
            game_request_exit();
        else if (action == GAME_MENU_SELECT)
            start_game();
        return;
    }
    if (paused)
        pause_input(event);
    else
        pending_buttons |= button_mask(event->button);
}

static void draw_menu(const struct game_framebuffer *fb)
{
    if (nes_error) {
        game_clear(fb, 0);
        game_draw_text(fb, 12, 12, "NES", 0xffff);
        game_draw_text(fb, 12, 55, nes_error, 0xffff);
        game_draw_text(fb, 12, 210, "PRESS ANY BUTTON TO RETURN", 0x8410);
    } else if (paused) {
        game_menu_draw(&pause_menu, fb);
    } else {
        game_menu_draw(&picker.menu, fb);
        if (!picker.menu.count)
            game_draw_text(fb, 12, 80, "COPY .NES FILES INTO REPRISE-NES", 0x8410);
    }
}

static void frame(const struct game_frame *frame)
{
    const struct game_framebuffer *screen = &frame->framebuffer;
    if (!screen->pixels)
        return;
    int hold = game_hold_locked();
    if (hold >= 0 && locked != (hold != 0)) {
        locked = hold != 0;
        if (locked && running && !choosing_region) {
            paused = true;
            pause_menu.selected = RESUME;
        }
        clear_input();
    }
    if (choosing_region) {
        struct game_wheel wheel = game_read_wheel();
        if (!locked)
            game_menu_scroll(&region_menu, &wheel);
        game_menu_draw(&region_menu, screen);
        return;
    }
    if (!running || paused || nes_error) {
        struct game_wheel wheel = game_read_wheel();
        if (!locked)
            game_menu_scroll(paused ? &pause_menu : &picker.menu, &wheel);
        draw_menu(screen);
        return;
    }

    uint32_t now = game_ticks_ms();
    uint32_t elapsed = now - last_frame_ms;
    last_frame_ms = now;
    frame_credit += (elapsed > 100 ? 100 : elapsed) * (InfoNES_Pal ? 50 : 60);
    unsigned frames = frame_credit / 1000;
    frame_credit %= 1000;
    /* Drop overdue work instead of bursting through a long input-stale batch. */
    if (frames > 2)
        frames = 2;
    for (unsigned i = 0; i < frames; ++i) {
        /* Keep CPU/PPU timing on skipped frames; render only the newest frame. */
        FrameCnt = i + 1 < frames ? 1 : 0;
        InfoNES_Cycle();
    }
    for (unsigned y = 0; y < 240; ++y) {
        uint16_t *row = screen->pixels + y * screen->stride_pixels;
        for (unsigned x = 0; x < 32; ++x)
            row[x] = row[x + 288] = 0;
        for (unsigned x = 0; x < 256; ++x)
            row[x + 32] = WorkFrame[y * 256 + x];
    }
}

static void shutdown(void)
{
    InfoNES_Fin();
    game_file_picker_close(&picker);
}

const struct game_app game_application = {
    .package_id = "reprise-nes",
    .init = init,
    .input = input,
    .frame = frame,
    .shutdown = shutdown,
};

void InfoNES_LoadFrame(void)
{
    /* The native callback copies the latest complete frame after catch-up. */
}

void InfoNES_PadState(DWORD *one, DWORD *two, DWORD *system)
{
    uint8_t pad = pending_buttons;
    for (unsigned button = GAME_BUTTON_MENU; button <= GAME_BUTTON_PLAY; ++button)
        if (held[button])
            pad |= button_mask(button);
    *one = pad;
    *two = *system = 0;
}

BYTE InfoNES_ReadPadBit(unsigned player, unsigned bit)
{
    BYTE value = ((player ? PAD2_Latch : PAD1_Latch) >> bit) & 1;
    /* Preserve quick taps until the ROM reads the corresponding button. */
    if (!player && bit < 8 && value)
        pending_buttons &= ~(1u << bit);
    return value;
}

void *InfoNES_MemoryCopy(void *destination, const void *source, int count)
{
    BYTE *out = destination;
    const BYTE *in = source;
    for (int i = 0; i < count; ++i)
        out[i] = in[i];
    return destination;
}

void *InfoNES_MemorySet(void *destination, int value, int count)
{
    BYTE *out = destination;
    for (int i = 0; i < count; ++i)
        out[i] = value;
    return destination;
}

int InfoNES_Menu(void)
{
    return -1;
}

void InfoNES_Wait(void)
{
}

void InfoNES_DebugPrint(char *message)
{
    (void)message;
}

void InfoNES_MessageBox(char *message, ...)
{
    (void)message;
    nes_error = "UNSUPPORTED ROM";
}

void InfoNES_SoundInit(void)
{
}

int InfoNES_SoundOpen(int samples, int rate)
{
    (void)samples;
    (void)rate;
    return 0;
}

void InfoNES_SoundClose(void)
{
}

void InfoNES_SoundOutput(int count, BYTE *a, BYTE *b, BYTE *c, BYTE *d, BYTE *e)
{
    (void)count;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
}
