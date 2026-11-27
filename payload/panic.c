/* SPDX-License-Identifier: GPL-3.0-only */

#include "panic.h"
#include "native/bindings.h"
#include "recovery.h"
#include "rvx/runtime.h"
#include "sad_ipod.h"
#include <stddef.h>

#define REG32(address) (*(volatile uint32_t *)(address))
#define LCD_BASE 0x38300000u
/* Display pipe: CONTROL bits 6, 5 and 4 enable layers 0 to 2, which it scans out
 * over whatever the panel holds. Cleared before the panel is written directly. */
#define DISP_CONTROL REG32(0x38900008u)
#define DISP_LAYERS 0x70u
#define DRAM_BASE 0x08000000u
#define DRAM_END 0x0c000000u
#define MAIN_HEAP_FREE_BYTES 0x0
#define MAIN_HEAP_TOTAL_BYTES 0x4
#define LCD_CONFIG REG32(LCD_BASE)
#define LCD_COMMAND REG32(LCD_BASE + 0x04)
#define LCD_DATA REG32(LCD_BASE + 0x40)
#define WIDTH 320u
#define HEIGHT 240u
#define WHITE 0xffffu
#define BACKGROUND 0x1803u

typedef char frame_layout_check
    [sizeof(struct panic_frame) == PANIC_FRAME_BYTES &&
             offsetof(struct panic_frame, exception_lr) == PANIC_EXLR &&
             offsetof(struct panic_frame, valid) == PANIC_VALID &&
             offsetof(struct panic_frame, sp) == PANIC_SP &&
             offsetof(struct panic_frame, stack_count) == PANIC_STACK_COUNT &&
             offsetof(struct panic_frame, stack) == PANIC_STACK
         ? 1
         : -1];

static uint16_t pixels[WIDTH];
static unsigned scan_y;

static void render_line(const struct panic_frame *frame);

/* Five-bit rows, most significant pixel first. */
static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-+:./";
static const uint8_t glyphs[][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14},
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
    {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14},      {7, 2, 2, 2, 2, 18, 12},
    {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
    {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},
    {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},
    {0, 0, 0, 31, 0, 0, 0},       {0, 4, 4, 31, 4, 4, 0},
    {0, 4, 4, 0, 4, 4, 0},        {0, 0, 0, 0, 0, 6, 6},
    {1, 2, 2, 4, 8, 8, 16}};

static void text(unsigned x, unsigned y, unsigned scale, const char *value)
{
    if (scan_y < y || scan_y >= y + 7 * scale)
        return;
    unsigned row = (scan_y - y) / scale;
    for (; *value; value++, x += 6 * scale) {
        char letter = *value;
        if (letter >= 'a' && letter <= 'z')
            letter -= 'a' - 'A';
        unsigned index = 0;
        while (alphabet[index] && alphabet[index] != letter)
            index++;
        if (!alphabet[index])
            continue;
        for (unsigned col = 0; col < 5; col++) {
            if (!(glyphs[index][row] & (16u >> col)))
                continue;
            for (unsigned dx = 0; dx < scale; dx++) {
                unsigned px = x + col * scale + dx;
                if (px < WIDTH)
                    pixels[px] = WHITE;
            }
        }
    }
}

static void sad_ipod(void)
{
    if (scan_y < 134 || scan_y >= 134 + sizeof sad_ipod_rows / sizeof *sad_ipod_rows)
        return;
    const uint8_t *row = sad_ipod_rows[scan_y - 134];
    for (unsigned x = 0; x < 54; x++)
        if (row[x / 8] & (0x80u >> (x % 8)))
            pixels[WIDTH - 12 - 54 + x] = WHITE;
}

static char *decimal(char *out, uint32_t value)
{
    char digits[10];
    unsigned n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n)
        *out++ = digits[--n];
    *out = 0;
    return out;
}

static char *copy(char *out, const char *text)
{
    while (*text)
        *out++ = *text++;
    *out = 0;
    return out;
}

static char *hex(char *out, uint32_t value)
{
    for (unsigned digit = 0; digit < 8; digit++)
        *out++ = "0123456789ABCDEF"[(value >> (28 - digit * 4)) & 15];
    *out = 0;
    return out;
}

/* Main heap free and total bytes. */
static void heap_line(unsigned y)
{
    char line[64];
    char *out = line;
    uint32_t heap = *(volatile uint32_t *)OSOS_MAIN_HEAP;
    if (heap < DRAM_BASE || heap > DRAM_END - 8 || heap & 3) {
        copy(out, "HEAP UNAVAILABLE");
    } else {
        out = copy(out, "HEAP FREE ");
        out = decimal(out, *(uint32_t *)(heap + MAIN_HEAP_FREE_BYTES));
        out = copy(out, " OF ");
        decimal(out, *(uint32_t *)(heap + MAIN_HEAP_TOTAL_BYTES));
    }
    text(12, y, 1, line);
}

static void field(unsigned x, unsigned y, unsigned scale, const char *label,
                  uint32_t value)
{
    char line[15];
    unsigned i = 0;
    while (label[i] && i < 5) {
        line[i] = label[i];
        i++;
    }
    while (i < 5)
        line[i++] = ' ';
    hex(line + i, value);
    text(x, y, scale, line);
}

static int wait_bits(uintptr_t address, uint32_t mask, uint32_t expected)
{
    for (unsigned remaining = 2000000; remaining; remaining--) {
        if ((REG32(address) & mask) == expected)
            return 1;
    }
    return 0;
}

static int lcd_config(uint32_t value)
{
    if (!wait_bits(LCD_BASE + 0x1c, 2, 2))
        return 0;
    for (volatile unsigned delay = 0; delay < 1000; delay++) {
    }
    LCD_CONFIG = value;
    return 1;
}

static int lcd_command(uint32_t value)
{
    if (!wait_bits(LCD_BASE + 0x1c, 0x10, 0))
        return 0;
    LCD_COMMAND = value;
    return 1;
}

static int lcd_data(uint32_t value)
{
    if (!wait_bits(LCD_BASE + 0x1c, 0x10, 0))
        return 0;
    LCD_DATA = value;
    return 1;
}

static int lcd_register(uint32_t command, uint32_t value)
{
    return lcd_command(command) && lcd_data(value);
}

static int lcd_range(uint32_t command, unsigned end)
{
    return lcd_command(command) && lcd_data(0) && lcd_data(0) && lcd_data(end >> 8) &&
           lcd_data(end & 255);
}

static int stop_lcd_dma(void)
{
    /* Only enabled DMAC0 channels targeting the LCD pixel port are stopped. */
    if (REG32(0x3c500048) & (1u << 25))
        return 1;
    for (unsigned channel = 0; channel < 8; channel++) {
        uintptr_t base = 0x38200100u + channel * 0x20;
        uint32_t config = REG32(base + 0x10);
        if (!(config & 1) || REG32(base + 4) != LCD_BASE + 0x40)
            continue;
        REG32(base + 0x10) = config | (1u << 18);
        if (!wait_bits(base + 0x10, 1u << 17, 0))
            return 0;
        REG32(base + 0x10) = config & ~1u;
        REG32(base + 8) = 0;
    }
    return 1;
}

static int show_pixels(const struct panic_frame *frame)
{
    /* Reuse RetailOS panel initialization and the active backlight. */
    REG32(0x3c500048) &= ~(1u << 1);  /* LCD clock */
    REG32(0x3c50004c) &= ~(1u << 12); /* GPIO clock */
    unsigned panel = (REG32(0x3cf000c4) >> 4) & 3;
    uint32_t timing = LCD_CONFIG & 0x80000007;

    /* Stop Apple's frame engine and layer scan-out before taking over the
     * command interface. */
    DISP_CONTROL &= ~DISP_LAYERS;
    REG32(LCD_BASE + 0x70) = 0;
    if (!stop_lcd_dma() || !wait_bits(LCD_BASE + 0x8c, 3, 0))
        return 0;
    if (!lcd_config(timing | (panel < 2 ? 0xc20 : 0xda8)))
        return 0;
    if (panel < 2) {
        if (!lcd_range(0x2a, WIDTH - 1) || !lcd_range(0x2b, HEIGHT - 1) ||
            !lcd_command(0x2c))
            return 0;
    } else {
        if (!lcd_register(0x210, 0) || !lcd_register(0x211, WIDTH - 1) ||
            !lcd_register(0x212, 0) || !lcd_register(0x213, HEIGHT - 1) ||
            !lcd_register(0x200, 0) || !lcd_register(0x201, 0) || !lcd_command(0x202))
            return 0;
    }
    if (!lcd_config(timing | 0x100db0))
        return 0;
    for (unsigned y = 0; y < HEIGHT; y++) {
        scan_y = y;
        render_line(frame);
        for (unsigned x = 0; x < WIDTH; x++) {
            if (!lcd_data(pixels[x]))
                return 0;
        }
    }
    return wait_bits(LCD_BASE + 0x1c, 2, 2);
}

static void register_field(unsigned x, unsigned y, unsigned index, uint32_t value)
{
    char label[4] = {'R', '0', 0, 0};
    if (index < 10) {
        label[1] += index;
    } else {
        label[1] = '1';
        label[2] = '0' + index - 10;
    }
    field(x, y, 1, label, value);
}

static void software_details(const struct panic_frame *frame)
{
    field(12, 52, 2, "LR", frame->exception_lr);
    field(12, 72, 1, "SP", frame->sp);
    field(166, 72, 1, "CPSR", frame->cpsr);
    heap_line(84);
    text(12, 96, 1, "REGISTERS");
    text(166, 96, 1, "STACK");
    if (frame->valid) {
        for (unsigned i = 0; i < 13; i++)
            register_field(12, 108 + i * 10, i, frame->registers[i]);
    } else {
        text(12, 108, 1, "UNAVAILABLE");
    }
    for (unsigned i = 0; i < frame->stack_count && i < PANIC_STACK_WORDS; i++) {
        unsigned offset = i * 4;
        char label[4] = {'+', "0123456789ABCDEF"[offset >> 4],
                         "0123456789ABCDEF"[offset & 15], 0};
        field(166, 108 + i * 10, 1, label, frame->stack[i]);
    }
    if (!frame->stack_count)
        text(166, 108, 1, "UNAVAILABLE");
}

static void render_line(const struct panic_frame *frame)
{
    for (unsigned x = 0; x < WIDTH; x++)
        pixels[x] = BACKGROUND;

    if (cfw_recovery_valid() && CFW_CRASH_RECORD->report_id) {
        text(12, 16, 2, "WE RAN INTO AN ERROR");
        sad_ipod();
        if (CFW_CRASH_RECORD->owner.hash) {
            char name[40];
            unsigned i = 0;
            for (; i < sizeof name - 1 && CFW_CRASH_RECORD->owner.name[i]; i++)
                name[i] = CFW_CRASH_RECORD->owner.name[i];
            name[i] = 0;
            text(12, 52, 1, name[0] ? name : "EXTENSION ERROR");
            text(12, 76, 1, "CENTER WILL RESTART WITHOUT");
            text(12, 90, 1, "THIS EXTENSION.");
        } else {
            text(12, 52, 1, "REPRISEOS");
            text(12, 76, 1, "CENTER WILL RESTART.");
        }
        char path[32];
        cfw_recovery_path(path, CFW_CRASH_RECORD->report_id);
        text(12, 116, 1, "REPORT WILL BE SAVED AFTER RESTART:");
        text(12, 130, 1, path);
        text(12, 158, 1, "OPEN AN ISSUE AND ATTACH THE REPORT:");
        text(12, 172, 1, "GITHUB.COM/REPRISEOS/REPRISEOS/ISSUES");
        text(12, 210, 2, "CENTER TO RESTART");
        return;
    }

    const char *reason = "CPU EXCEPTION";
    uint32_t pc = frame->exception_lr;
    switch (frame->type) {
    case 1:
        reason = "UNDEFINED INSTRUCTION";
        pc -= (frame->spsr & 0x20) ? 2 : 4;
        break;
    case 3:
        reason = "PREFETCH ABORT";
        pc -= 4;
        break;
    case 4:
        reason = "DATA ABORT";
        pc -= 8;
        break;
    case 5:
        reason = "RESERVED VECTOR";
        break;
    case 7:
        reason = "UNEXPECTED FIQ";
        pc -= 4;
        break;
    case 8:
        reason = "SOFTWARE ABORT";
        break;
    }
    text(12, 10, 2, "OS PANIC");
    text(206, 10, 1, "CENTER TO RESTART");
    text(12, 32, 2, reason);
    if (frame->type == 8) {
        software_details(frame);
        return;
    }
    if (frame->valid) {
        field(12, 54, 2, "PC", pc);
        field(12, 74, 2, "EXLR", frame->exception_lr);
    } else {
        text(12, 54, 1, "EXCEPTION STACK UNREADABLE");
    }
    if (frame->type == 4) {
        field(12, 94, 2, "DFAR", frame->dfar);
        field(12, 114, 2, "DFSR", frame->dfsr);
    } else if (frame->type == 3) {
        field(12, 94, 2, "IFSR", frame->ifsr);
    }
    field(12, 134, 1, "CPSR", frame->cpsr);
    field(166, 134, 1, "SPSR", frame->spsr);
    heap_line(146);
    if (frame->valid) {
        for (unsigned i = 0; i < 13; i++)
            register_field(i < 7 ? 12 : 166, 158 + (i % 7) * 9, i, frame->registers[i]);
    } else {
        text(12, 168, 1, "SAVED REGISTERS UNAVAILABLE");
    }
}

void cfw_panic_screen(const struct panic_frame *frame)
{
    REG32(0x3c800000) = 0;
    /* The record is committed before the runtime's lookup runs, so a fault in
     * the lookup leaves a report for the next start. */
    cfw_recovery_capture(frame, NULL);
    struct cfw_crash_owner owner;
    if (cfw_rvx_crash_owner(frame, &owner))
        cfw_recovery_capture(frame, &owner);
    uint32_t backlight[4];
    cfw_panic_backlight(backlight);
    cfw_recovery_backlight(backlight);
    show_pixels(frame);
    cfw_recovery_wait();
}
