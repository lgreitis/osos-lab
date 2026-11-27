/* SPDX-License-Identifier: MIT */

/* Draws a greeting, the time since start and how long one sleep took, the
 * clock, the language, the platform and the power source, the wheel
 * direction, how often it ran, kept in Apple's save store, what the music
 * player does and the backlight; Center plays a tone computed at start and
 * dims while held, a wheel touch plays the beep.wav of the package, turning
 * the wheel changes the backlight; the badge of art/badge.png sits at the top
 * right; Play, Next and Previous go to the music player; Menu exits. */

#include "eapp_draw.h"
#include "images.h"

static char seconds[12], runs[12], track[26], times[20], slept[12], clock_text[24];
static char language[8], name[16], level_text[8];
static struct eapp_sound *tone, *beep;
static struct eapp_bitmap *badge;
static char *number(char *out, uint32_t value);
static int level = -1; /* The backlight the wheel set, -1 while untouched. */

/* 300 ms of a 441 Hz square wave, 16-bit mono 44100 Hz, in a heap block
 * that stays the app's while the sound exists. */
static struct eapp_sound *make_tone(void)
{
    enum { RATE = 44100, FRAMES = RATE * 3 / 10, HALF_PERIOD = 50 };
    int16_t *pcm = eapp_alloc(FRAMES * sizeof *pcm);
    if (!pcm)
        return NULL;
    for (unsigned i = 0; i < FRAMES; ++i)
        pcm[i] = (i / HALF_PERIOD) & 1 ? 6000 : -6000;
    struct eapp_sound *sound = eapp_sound_create(pcm, FRAMES * sizeof *pcm, RATE, 1, 16);
    if (!sound)
        eapp_free(pcm);
    return sound;
}

static void init(void)
{
    eapp_log("hello: init");
    uint32_t start = eapp_ticks_ms();
    eapp_sleep_ms(500);
    number(slept, eapp_ticks_ms() - start);
    const char *code = eapp_language();
    unsigned n = 0;
    for (; code && code[n] && n < sizeof language - 1; ++n)
        language[n] = code[n] == '_' ? '-' : code[n];
    language[n] = 0;
    if (eapp_name(name, sizeof name) < 0)
        name[0] = 0;
    tone = make_tone();
    beep = eapp_sound_load("beep.wav");
    badge = eapp_bitmap_load(EAPP_IMAGE_BADGE);
    struct eapp_save *save = eapp_save_open("runs");
    if (!save)
        return;
    uint32_t count = 0;
    eapp_save_read(save, &count, sizeof count, 0);
    ++count;
    eapp_save_write(save, &count, sizeof count);
    eapp_save_close(save);
    unsigned i = sizeof runs - 1;
    runs[i] = 0;
    do {
        runs[--i] = '0' + count % 10;
        count /= 10;
    } while (count && i);
    for (unsigned j = 0; (runs[j] = runs[i + j]); ++j)
        ;
}

static void input(const struct eapp_input_event *event)
{
    if (event->button == EAPP_BUTTON_CENTER)
        eapp_set_backlight_dimmed(event->type == EAPP_INPUT_PRESS);
    if (event->type != EAPP_INPUT_PRESS)
        return;
    if (event->button == EAPP_BUTTON_MENU)
        eapp_request_exit();
    else if (event->button == EAPP_BUTTON_CENTER)
        eapp_sound_play(tone);
    else if (event->button == EAPP_BUTTON_WHEEL)
        eapp_sound_play(beep);
}

/* Writes `value` in decimal at `out` and returns the end. */
static char *number(char *out, uint32_t value)
{
    char digits[11];
    unsigned i = 0;
    do {
        digits[i++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (i)
        *out++ = digits[--i];
    *out = 0;
    return out;
}

static char *minutes_seconds(char *out, uint32_t milliseconds)
{
    uint32_t total = milliseconds / 1000;
    out = number(out, total / 60);
    *out++ = ':';
    *out++ = '0' + total % 60 / 10;
    *out++ = '0' + total % 10;
    *out = 0;
    return out;
}

static void shutdown(void)
{
    eapp_sound_free(tone);
    eapp_sound_free(beep);
    eapp_bitmap_free(badge);
}

static void frame(const struct eapp_frame *frame)
{
    const struct eapp_framebuffer *fb = &frame->framebuffer;
    if (!fb->pixels)
        return;
    for (unsigned y = 0; y < fb->height; ++y)
        for (unsigned x = 0; x < fb->width; ++x)
            fb->pixels[y * fb->stride_pixels + x] = 0x0010;

    eapp_draw_text(fb, 20, 8, "HELLO FROM REVOLAOS", 0xffff, 1);
    if (badge)
        eapp_draw_bitmap(fb, EAPP_SCREEN_WIDTH - 8 - EAPP_IMAGE_BADGE_WIDTH, 4, badge);
    eapp_draw_text(fb, 20, 24, "NAME:", 0xffff, 1);
    eapp_draw_text(fb, 56, 24, name, 0xffff, 1);

    uint32_t ticks = eapp_ticks_ms() / 1000;
    unsigned i = sizeof seconds - 1;
    seconds[i] = 0;
    do {
        seconds[--i] = '0' + ticks % 10;
        ticks /= 10;
    } while (ticks && i);
    eapp_draw_text(fb, 20, 40, "SECONDS:", 0xffff, 1);
    eapp_draw_text(fb, 74, 40, seconds + i, 0xffff, 1);
    eapp_draw_text(fb, 110, 40, "SLEPT:", 0xffff, 1);
    eapp_draw_text(fb, 152, 40, slept, 0xffff, 1);

    struct eapp_date_time now;
    eapp_date_time(&now);
    char *end = eapp_format_time(clock_text, &now);
    *end++ = ' ';
    *end++ = ' ';
    end = number(end, now.year);
    *end++ = '-';
    *end++ = '0' + now.month / 10;
    *end++ = '0' + now.month % 10;
    *end++ = '-';
    *end++ = '0' + now.day / 10;
    *end++ = '0' + now.day % 10;
    *end = 0;
    eapp_draw_text(fb, 20, 56, "CLOCK:", 0xffff, 1);
    eapp_draw_text(fb, 62, 56, clock_text, 0xffff, 1);

    enum eapp_platform platform = eapp_platform();
    eapp_draw_text(fb, 20, 72, "LANGUAGE:", 0xffff, 1);
    eapp_draw_text(fb, 80, 72, language, 0xffff, 1);
    eapp_draw_text(fb, 128, 72,
                   platform == EAPP_PLATFORM_CLASSIC      ? "CLASSIC"
                   : platform == EAPP_PLATFORM_NANO_3G    ? "NANO 3G"
                   : platform == EAPP_PLATFORM_IPOD_VIDEO ? "IPOD VIDEO"
                                                          : "UNKNOWN",
                   0xffff, 1);
    eapp_draw_text(fb, 200, 72, eapp_external_power() ? "CABLE" : "BATTERY", 0xffff, 1);

    const struct eapp_wheel *wheel = &frame->wheel;
    uint8_t direction = eapp_wheel_directions(wheel, EAPP_WHEEL_TOP_POSITION);
    if (wheel->delta) {
        int next = (level < 0 ? 50 : level) + wheel->delta * 5;
        level = next < 0 ? 0 : next > 100 ? 100 : next;
        eapp_set_backlight_level(level);
    }
    eapp_draw_text(fb, 20, 88, "WHEEL:", 0xffff, 1);
    eapp_draw_text(fb, 62, 88, direction & EAPP_DIRECTION_UP ? "UP" : "", 0xffff, 1);
    eapp_draw_text(fb, 80, 88, direction & EAPP_DIRECTION_DOWN ? "DOWN" : "", 0xffff, 1);
    eapp_draw_text(fb, 110, 88, direction & EAPP_DIRECTION_LEFT ? "LEFT" : "", 0xffff, 1);
    eapp_draw_text(fb, 140, 88, direction & EAPP_DIRECTION_RIGHT ? "RIGHT" : "", 0xffff, 1);

    eapp_draw_text(fb, 20, 104, "HOLD:", 0xffff, 1);
    eapp_draw_text(fb, 56, 104, eapp_hold_locked() ? "ON" : "OFF", 0xffff, 1);
    eapp_draw_text(fb, 110, 104, "RUNS:", 0xffff, 1);
    eapp_draw_text(fb, 146, 104, runs, 0xffff, 1);

    eapp_draw_text(fb, 20, 120, "SOUND:", 0xffff, 1);
    eapp_draw_text(fb, 62, 120,
                   eapp_sound_playing(tone) || eapp_sound_playing(beep) ? "ON" : "OFF",
                   0xffff, 1);
    if (level >= 0)
        number(level_text, level);
    eapp_draw_text(fb, 110, 120, "BACKLIGHT:", 0xffff, 1);
    eapp_draw_text(fb, 176, 120, level >= 0 ? level_text : "USER", 0xffff, 1);

    enum eapp_music_state state = eapp_music_state();
    eapp_draw_text(fb, 20, 136, "MUSIC:", 0xffff, 1);
    eapp_draw_text(fb, 62, 136,
                   state == EAPP_MUSIC_PLAYING  ? "PLAYING"
                   : state == EAPP_MUSIC_PAUSED ? "PAUSED"
                                                : "STOPPED",
                   0xffff, 1);
    if (eapp_music_has_playlist()) {
        end = minutes_seconds(times, eapp_music_position());
        *end++ = ' ';
        minutes_seconds(end, eapp_music_duration());
        eapp_draw_text(fb, 116, 136, times, 0xffff, 1);
        end = number(track, eapp_music_track_index() + 1);
        *end++ = ' ';
        *end++ = 'O';
        *end++ = 'F';
        *end++ = ' ';
        number(end, eapp_music_track_count());
        eapp_draw_text(fb, 20, 152, "TRACK:", 0xffff, 1);
        eapp_draw_text(fb, 62, 152, track, 0xffff, 1);
    }

    eapp_draw_text(fb, 20, 196, "CENTER: TONE AND DIM  WHEEL: BEEP", 0x8410, 1);
    eapp_draw_text(fb, 20, 212, "TURN: BACKLIGHT  MENU: EXIT", 0x8410, 1);
}

const struct eapp eapp_application = {
    .init = init,
    .input = input,
    .frame = frame,
    .shutdown = shutdown,
};
