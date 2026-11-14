/* SPDX-License-Identifier: GPL-3.0-only */
#include "game_api.h"
#include "game_ui.h"
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "rom.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static unsigned char image[16 + 16384 + 8192];
static unsigned image_size = sizeof(image), allocations, now;
static unsigned emulated_frames;
static unsigned scanlines, cpu_cycles, wheel_reads;
static void (*mapper_vsync)(void);
static void (*mapper_hsync)(void);
static int exit_requested, hold, roms_available;
static struct game_wheel wheel;
static int scan_error_after = -1, allocation_fail_after = -1, many_files;
static unsigned directory_opens, directory_closes;

struct game_directory {
    unsigned index;
    int active;
};
static struct game_directory directory;
static const struct game_directory_entry entries[] = {
    {"Zeta.NES", 0},   {"notes.txt", 0},     {"roms/synthetic.nes", 0},
    {"folder.nes", 1}, {".hidden.nes", 0},   {"roms/._synthetic.nes", 0},
    {"data.WAD", 0},   {"extra.wad.txt", 0}, {".hidden/other.nes", 0},
};

struct game_directory *game_directory_open(const char *path, int recursive)
{
    assert(!*path && recursive && !directory.active);
    directory = (struct game_directory){0, 1};
    ++directory_opens;
    return &directory;
}

int game_directory_read(struct game_directory *dir, struct game_directory_entry *entry)
{
    assert(dir == &directory && dir->active);
    if (scan_error_after >= 0 && dir->index == (unsigned)scan_error_after)
        return -1;
    if (!roms_available)
        return 0;
    if (many_files) {
        if (dir->index == 65)
            return 0;
        snprintf(entry->name, sizeof(entry->name), "%02u.nes", dir->index++);
        entry->is_directory = 0;
        return 1;
    }
    if (dir->index == sizeof(entries) / sizeof(entries[0]))
        return 0;
    *entry = entries[dir->index++];
    return 1;
}

void game_directory_close(struct game_directory *dir)
{
    assert(dir == &directory && dir->active);
    dir->active = 0;
    ++directory_closes;
}

struct game_file {
    unsigned position;
    const unsigned char *bytes;
    unsigned size;
};
static struct game_file file;

void *game_alloc(size_t size)
{
    if (allocation_fail_after == 0)
        return NULL;
    if (allocation_fail_after > 0)
        --allocation_fail_after;
    void *p = malloc(size);
    if (p)
        ++allocations;
    return p;
}

void game_free(void *p)
{
    if (p) {
        --allocations;
        free(p);
    }
}

uint32_t game_ticks_ms(void)
{
    return now;
}

static void count_frame(void)
{
    ++emulated_frames;
    mapper_vsync();
}

static void count_scanline(void)
{
    ++scanlines;
    cpu_cycles += InfoNES_ScanlineCycles;
    mapper_hsync();
}

static void advance(const struct game_frame *frame, unsigned ms)
{
    now += ms;
    game_application.frame(frame);
}

void game_sleep_ms(uint32_t ms)
{
    now += ms;
}

struct game_wheel game_read_wheel(void)
{
    ++wheel_reads;
    return wheel;
}

int game_hold_locked(void)
{
    return hold;
}

void game_request_exit(void)
{
    exit_requested = 1;
}

struct game_file *game_file_open(const char *path)
{
    if (!strcmp(path, "roms/synthetic.nes"))
        file = (struct game_file){0, image, image_size};
    else
        return NULL;
    return &file;
}

int game_file_read(struct game_file *f, void *out, size_t size)
{
    if (size > f->size - f->position)
        size = f->size - f->position;
    if (size > 31)
        size = 31; /* Exercise short successful reads. */
    memcpy(out, f->bytes + f->position, size);
    f->position += size;
    return size;
}

uint32_t game_file_size(const struct game_file *f)
{
    return f->size;
}

void game_file_close(struct game_file *f)
{
    (void)f;
}

static void button(unsigned id)
{
    struct game_input_event event = {id, GAME_INPUT_PRESS, now};
    game_application.input(&event);
    event.type = GAME_INPUT_RELEASE;
    game_application.input(&event);
}

static void set_button(unsigned id, int pressed)
{
    struct game_input_event event = {
        id, pressed ? GAME_INPUT_PRESS : GAME_INPUT_RELEASE, now};
    game_application.input(&event);
}

static void pause_game(const struct game_frame *frame)
{
    hold = 1;
    game_application.frame(frame);
    hold = 0;
    game_application.frame(frame);
}

static void choose_pause_item(unsigned index)
{
    for (unsigned i = 0; i < index; ++i)
        button(GAME_BUTTON_NEXT);
    button(GAME_BUTTON_CENTER);
}

int main(void)
{
    struct game_file_picker picker = {0};
    roms_available = 1;
    assert(game_file_picker_open(&picker, "TEST", "", ".nes") == 0);
    assert(picker.menu.count == 2);
    assert(!strcmp(picker.menu.items[0].value, "roms/synthetic.nes"));
    assert(!strcmp(picker.menu.items[0].label, "synthetic"));
    assert(!strcmp(picker.menu.items[1].value, "Zeta.NES"));
    struct game_input_event choose = {GAME_BUTTON_NEXT, GAME_INPUT_PRESS, 0};
    assert(game_menu_input(&picker.menu, &choose) == GAME_MENU_NONE);
    assert(picker.menu.selected == 1);
    choose.button = GAME_BUTTON_CENTER;
    assert(game_menu_input(&picker.menu, &choose) == GAME_MENU_SELECT);
    game_file_picker_close(&picker);
    assert(allocations == 0);
    assert(game_file_picker_open(&picker, "WADS", "", ".wad") == 0);
    assert(picker.menu.count == 1 && !strcmp(picker.menu.items[0].value, "data.WAD"));
    game_file_picker_close(&picker);
    scan_error_after = 3;
    assert(game_file_picker_open(&picker, "TEST", "", ".nes") == -1);
    assert(allocations == 0 && !picker.menu.count);
    scan_error_after = -1;
    allocation_fail_after = 1;
    assert(game_file_picker_open(&picker, "TEST", "", ".nes") == -1);
    assert(allocations == 0 && !picker.menu.count);
    allocation_fail_after = -1;
    many_files = 1;
    assert(game_file_picker_open(&picker, "TEST", "", ".nes") == -2);
    assert(allocations == 0 && !picker.menu.count);
    many_files = 0;
    roms_available = 0;
    assert(directory_opens == directory_closes);
    static const unsigned dirs[8] = {1, 3, 2, 6, 4, 12, 8, 9};
    for (unsigned i = 0; i < 8; ++i) {
        struct game_wheel touch = {0, (uint8_t)(-i * 32), 1};
        assert(game_wheel_directions(&touch, 0) == dirs[i]);
        touch.touched = 0;
        assert(game_wheel_directions(&touch, 0) == 0);
    }
    memcpy(image, "NES\032", 4);
    image[4] = image[5] = 1;
    /* Original program: backdrop and NMI; handler counts frames and reads $4016. */
    const unsigned char code[] = {
        0x78, 0xd8, 0xad, 0x15, 0x40, 0xa9, 0x3f, 0x8d, 0x06, 0x20, 0xa9,
        0x00, 0x8d, 0x06, 0x20, 0xa9, 0x21, 0x8d, 0x07, 0x20, 0xa9, 0x0a,
        0x8d, 0x01, 0x20, 0xa9, 0x80, 0x8d, 0x00, 0x20, 0x4c, 0x1e, 0x80,
    };
    memcpy(image + 16, code, sizeof(code));
    for (unsigned i = 0x3ffa; i < 0x4000; i += 2) {
        image[16 + i] = 0;
        image[16 + i + 1] = 0x80;
    }
    image[16 + 0x3ffb] = 0x81;
    const unsigned char nmi[] = {
        0xe6, 0x00, 0xa9, 0x01, 0x8d, 0x16, 0x40, 0xa9, 0x00, 0x8d, 0x16,
        0x40, 0xa2, 0x08, 0xad, 0x16, 0x40, 0x4a, 0x66, 0x01, 0xca, 0xd0,
        0xf7, 0xa5, 0x01, 0x85, 0x03, 0x05, 0x02, 0x85, 0x02, 0x40,
    };
    memcpy(image + 16 + 0x100, nmi, sizeof(nmi));
    uint16_t *pixels = calloc(320 * 240 + 2, sizeof(*pixels));
    assert(pixels);
    pixels[0] = pixels[320 * 240 + 1] = 0x1234;
    struct game_frame frame = {{pixels + 1, 320, 240, 320}};
    game_application.init();
    button(GAME_BUTTON_CENTER);
    game_application.frame(&frame);
    assert(!nes_error && allocations == 0 && emulated_frames == 0);
    game_application.shutdown();
    roms_available = 1;
    game_application.init();
    button(GAME_BUTTON_CENTER);
    assert(!nes_error && allocations == 4);
    game_application.frame(&frame);
    assert(emulated_frames == 0);
    button(GAME_BUTTON_CENTER); /* PAL video system. */
    assert(InfoNES_Pal);
    mapper_vsync = MapperVSync;
    MapperVSync = count_frame;
    mapper_hsync = MapperHSync;
    MapperHSync = count_scanline;
    game_application.frame(&frame);
    assert(emulated_frames == 1);
    assert(RAM[0] == 1);
    game_application.frame(&frame);
    assert(emulated_frames == 1);
    unsigned before_cycles = cpu_cycles, before_scanlines = scanlines;
    unsigned before_reads = wheel_reads;
    for (unsigned i = 0; i < 120; ++i)
        advance(&frame, 33);
    assert(emulated_frames == 1 + 3960 * 50 / 1000);
    assert(RAM[0] == emulated_frames);
    assert(wheel_reads == before_reads);
    assert(scanlines - before_scanlines == 198 * 312);
    assert(cpu_cycles - before_cycles == 198 * 312 * 1705 / 16);
    unsigned before_stall = emulated_frames;
    advance(&frame, 2000);
    assert(emulated_frames == before_stall + 2);
    assert(PPU_Scanline == SCAN_VBLANK_START + 1);
    assert(pixels[0] == 0x1234 && pixels[320 * 240 + 1] == 0x1234);
    assert(pixels[1 + 100 * 320 + 100] != 0);
    assert(pixels[1 + 100 * 320] == 0);
    /* Touch never drives gameplay, even at every cardinal/diagonal position. */
    static const uint8_t positions[8] = {61, 29, 253, 221, 189, 157, 125, 93};
    RAM[2] = 0;
    for (unsigned i = 0; i < 8; ++i) {
        wheel = (struct game_wheel){0, positions[i], 1};
        advance(&frame, 20);
        assert(PAD1_Latch == 0);
    }
    assert(RAM[2] == 0 && wheel_reads == before_reads);
    static const unsigned buttons[] = {GAME_BUTTON_NEXT, GAME_BUTTON_PREVIOUS,
                                       GAME_BUTTON_CENTER, GAME_BUTTON_PLAY,
                                       GAME_BUTTON_MENU};
    static const unsigned masks[] = {0x80, 0x40, 1, 0x20, 2};
    for (unsigned i = 0; i < 5; ++i) {
        for (unsigned tap = 0; tap < 2; ++tap) {
            RAM[2] = 0;
            button(buttons[i]);
            unsigned before = emulated_frames;
            game_application.frame(&frame);
            assert(emulated_frames == before);
            for (unsigned j = 0; j < 3; ++j)
                advance(&frame, 20);
            assert(RAM[2] == masks[i]);
            assert(RAM[3] == 0);
        }
    }
    /* A ROM that temporarily stops polling must not lose the pending press. */
    PPU_R0 &= ~R0_NMI_VB;
    advance(&frame, 20);
    RAM[2] = 0;
    pause_game(&frame);
    choose_pause_item(1); /* Start. */
    for (unsigned i = 0; i < 5; ++i)
        advance(&frame, 20);
    assert(PAD1_Latch & 8);
    PPU_R0 |= R0_NMI_VB;
    for (unsigned i = 0; i < 3; ++i)
        advance(&frame, 20);
    assert(RAM[2] & 8);
    assert(!(PAD1_Latch & 8));
    /* Hold clears queued input; navigating/resuming must not leak buttons. */
    button(GAME_BUTTON_NEXT);
    hold = 1;
    game_application.frame(&frame);
    unsigned before_pause = emulated_frames;
    button(GAME_BUTTON_CENTER); /* Locked menu ignores clicks. */
    advance(&frame, 5000);
    assert(emulated_frames == before_pause);
    hold = 0;
    game_application.frame(&frame);
    choose_pause_item(0);
    RAM[2] = 0;
    for (unsigned i = 0; i < 3; ++i)
        advance(&frame, 20);
    assert(RAM[2] == 0);
    assert(pixels[0] == 0x1234 && pixels[320 * 240 + 1] == 0x1234);
    /* Held horizontal clicks and A reach every controller read together. */
    for (unsigned side = 0; side < 2; ++side) {
        unsigned direction = side ? GAME_BUTTON_PREVIOUS : GAME_BUTTON_NEXT;
        set_button(direction, 1);
        set_button(GAME_BUTTON_CENTER, 1);
        advance(&frame, 20);
        for (unsigned i = 0; i < 60; ++i) {
            advance(&frame, 20);
            assert(RAM[3] == ((side ? 0x40 : 0x80) | 1));
        }
        set_button(direction, 0);
        set_button(GAME_BUTTON_CENTER, 0);
        for (unsigned i = 0; i < 3; ++i)
            advance(&frame, 20);
        assert(RAM[3] == 0);
    }
    /* Menu can be reassigned to Up and back to B without pausing gameplay. */
    for (unsigned mode = 0; mode < 2; ++mode) {
        pause_game(&frame);
        choose_pause_item(2);
        button(GAME_BUTTON_MENU); /* Back resumes. */
        RAM[2] = 0;
        button(GAME_BUTTON_MENU);
        for (unsigned i = 0; i < 3; ++i)
            advance(&frame, 20);
        assert(RAM[2] == (mode ? 2 : 0x10));
        assert(RAM[3] == 0);
    }
    /* Each Start menu selection sends a fresh press and returns to the game. */
    for (unsigned i = 0; i < 2; ++i) {
        pause_game(&frame);
        choose_pause_item(1);
        RAM[2] = 0;
        for (unsigned j = 0; j < 3; ++j)
            advance(&frame, 20);
        assert(RAM[2] == 8 && RAM[3] == 0);
    }
    now = UINT32_MAX - 10;
    game_application.frame(&frame);
    unsigned before_wrap = emulated_frames;
    advance(&frame, 40);
    assert(emulated_frames == before_wrap + 2);
    pause_game(&frame);
    choose_pause_item(3); /* Game list. */
    assert(allocations == 2);
    button(GAME_BUTTON_CENTER);
    assert(allocations == 4);
    button(GAME_BUTTON_NEXT);
    button(GAME_BUTTON_CENTER);
    assert(!InfoNES_Pal);
    mapper_vsync = MapperVSync;
    MapperVSync = count_frame;
    mapper_hsync = MapperHSync;
    MapperHSync = count_scanline;
    game_application.frame(&frame);
    before_scanlines = scanlines;
    unsigned before_ntsc = emulated_frames;
    for (unsigned i = 0; i < 100; ++i)
        advance(&frame, 10);
    assert(emulated_frames - before_ntsc == 60);
    assert(scanlines - before_scanlines == 60 * 262);
    assert(RAM[0] == 61);
    assert(!exit_requested);
    pause_game(&frame);
    choose_pause_item(4);
    assert(exit_requested);
    game_application.shutdown();
    assert(allocations == 0);
    image[9] = 1;
    assert(InfoNES_Load("roms/synthetic.nes") == 0);
    InfoNES_ReleaseRom();
    image[9] = 0;
    image_size = 20;
    assert(InfoNES_Load("roms/synthetic.nes") == -1 && allocations == 0);
    image_size = sizeof(image);
    image[7] = 8;
    assert(InfoNES_Load("roms/synthetic.nes") == -1 && allocations == 0);
    image[7] = 0;
    image[6] = 0x30;
    image[5] = 0;
    assert(InfoNES_Load("roms/synthetic.nes") == -1 && allocations == 0);
    image[6] = 0xf0;
    image[5] = 1;
    assert(InfoNES_Load("roms/synthetic.nes") == -1 && allocations == 0);
    assert(directory_opens == directory_closes);
    free(pixels);
    puts("NES synthetic-ROM, timing, video, controls, lifecycle and rejection checks "
         "passed");
}
