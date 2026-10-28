/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "custom_eq.h"
#include "eq_coefficients.h"

#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM
#define PATCH_COPY(...)
#define PATCH_CALL(...)

/* Factories are linked but not executed by this input/drawing contract test. */
#define OSOS_VIEW_INIT_OPTIONS 0
#define OSOS_CPP_NEW 0
#define OSOS_VIEW_CONSTRUCT 0
#define OSOS_REGISTER_VIEW_FACTORY 0
#define OSOS_GRAPHICS_CONSTRUCT construct_context
#define OSOS_VIEW_BIND_GRAPHICS bind_context
#define OSOS_VIEW_LOCAL_RECT local_rect

static unsigned forwarded, invalidated, decoded, rectangles;
static unsigned text_draws;
static int dark, text_font, pen_x, pen_y, draw_mode;
static unsigned char draw_color[4];
static unsigned char pixels[217][320][3];
static const char *gain_labels[49], *frequency_labels[32], *q_labels[38];
static uint32_t values[CFW_EQ_FIELDS];
static unsigned changes, commits, snapshots, version;
static int rejected, locked, example_response;
static unsigned native_hold_calls;
static void change_value(uint32_t field, uint32_t value);

static void commit_values(void)
{
    commits++;
}
const struct cfw_ui_model cfw_eq_model = {values, change_value, commit_values};
#define FIELD(slot_, title_, labels_, count_)                                          \
    [slot_] = {.slot = slot_,                                                          \
               .title = title_,                                                        \
               .labels = labels_,                                                      \
               .count = count_,                                                        \
               .model = &cfw_eq_model}
static const char *const type_labels[] = {"Off", "Peaking", "Low shelf", "High shelf"};
static const char *const precut_labels[] = {"0.0 dB"};
const struct cfw_ui_field cfw_eq_fields[CFW_EQ_FIELDS] = {
    FIELD(CFW_EQ_PRECUT, "Precut", precut_labels, 1),
    FIELD(CFW_EQ_BAND1_TYPE, "Type", type_labels, 4),
    FIELD(CFW_EQ_BAND1_GAIN, "Gain", gain_labels, 49),
    FIELD(CFW_EQ_BAND1_FREQUENCY, "Frequency", frequency_labels, 32),
    FIELD(CFW_EQ_BAND1_Q, "Q", q_labels, 38),
    FIELD(CFW_EQ_BAND2_TYPE, "Type", type_labels, 4),
    FIELD(CFW_EQ_BAND2_GAIN, "Gain", gain_labels, 49),
    FIELD(CFW_EQ_BAND2_FREQUENCY, "Frequency", frequency_labels, 32),
    FIELD(CFW_EQ_BAND2_Q, "Q", q_labels, 38),
    FIELD(CFW_EQ_BAND3_TYPE, "Type", type_labels, 4),
    FIELD(CFW_EQ_BAND3_GAIN, "Gain", gain_labels, 49),
    FIELD(CFW_EQ_BAND3_FREQUENCY, "Frequency", frequency_labels, 32),
    FIELD(CFW_EQ_BAND3_Q, "Q", q_labels, 38),
    FIELD(CFW_EQ_BAND4_TYPE, "Type", type_labels, 4),
    FIELD(CFW_EQ_BAND4_GAIN, "Gain", gain_labels, 49),
    FIELD(CFW_EQ_BAND4_FREQUENCY, "Frequency", frequency_labels, 32),
    FIELD(CFW_EQ_BAND4_Q, "Q", q_labels, 38),
    FIELD(CFW_EQ_BAND5_TYPE, "Type", type_labels, 4),
    FIELD(CFW_EQ_BAND5_GAIN, "Gain", gain_labels, 49),
    FIELD(CFW_EQ_BAND5_FREQUENCY, "Frequency", frequency_labels, 32),
    FIELD(CFW_EQ_BAND5_Q, "Q", q_labels, 38),
};
#undef FIELD
const int32_t cfw_eq_gain[49] = {-120, -115, -110, -105, -100, -95, -90, -85, -80, -75,
                                 -70,  -65,  -60,  -55,  -50,  -45, -40, -35, -30, -25,
                                 -20,  -15,  -10,  -5,   0,    5,   10,  15,  20,  25,
                                 30,   35,   40,   45,   50,   55,  60,  65,  70,  75,
                                 80,   85,   90,   95,   100,  105, 110, 115, 120};

static void change_value(uint32_t field, uint32_t value)
{
    assert(field < CFW_EQ_FIELDS && value < cfw_eq_fields[field].count);
    changes++;
    if (!rejected) {
        values[field] = value;
        version++;
    }
}

uint32_t cfw_eq_snapshot(int32_t out[CFW_EQ_BANDS][5], uint32_t *rate)
{
    snapshots++;
    *rate = 44100;
    if (example_response) {
        assert(cfw_eq_coefficients(out[0], CFW_EQ_LOW_SHELF, 60, 7, 75, 65, *rate));
        assert(cfw_eq_coefficients(out[1], CFW_EQ_PEAKING, 160, 12, 70, 0, *rate));
        assert(cfw_eq_coefficients(out[2], CFW_EQ_HIGH_SHELF, 40, 19, -5, 0, *rate));
        for (unsigned i = 3; i < CFW_EQ_BANDS; i++)
            assert(cfw_eq_coefficients(out[i], CFW_EQ_OFF, 1000, 7, 0, 0, *rate));
        return version;
    }
    for (unsigned i = 0; i < CFW_EQ_BANDS; i++)
        assert(cfw_eq_coefficients(out[i], 1, 1000, 7,
                                   cfw_eq_gain[values[CFW_EQ_BAND1_GAIN + i * 4]], 0,
                                   *rate));
    return version;
}

static int osos_hold_locked(void)
{
    return locked;
}

static void osos_system_hold_changed(void *system, int hold)
{
    (void)system;
    locked = hold;
    native_hold_calls++;
}

static uint32_t osos_controller_activate(void *controller, uint32_t entering)
{
    (void)controller;
    return entering;
}

static uint32_t osos_controller_deactivate(void *controller, uint32_t leaving)
{
    (void)controller;
    assert(commits);
    return leaving;
}

static unsigned wheel_constructions;
static void *wheel_state;
static int wheel_primed, wheel_remainder;

static void *construct_context(void *context)
{
    return context;
}

static void bind_context(void *view, void *context)
{
    (void)view;
    (void)context;
}

static void local_rect(void *view, int32_t *rect)
{
    (void)view;
    rect[0] = 0;
    rect[1] = 0;
    rect[2] = 217;
    rect[3] = 320;
}

static void osos_graphics_set_color(void *context, const unsigned char *color)
{
    (void)context;
    assert(color[3] == 255);
    memcpy(draw_color, color, 4);
}

static void osos_graphics_fill_rect_unmapped(void *context, const int32_t *rect)
{
    (void)context;
    assert(rect[0] >= 0 && rect[1] >= 0);
    assert(rect[2] <= 217 && rect[3] <= 320);
    assert(rect[2] > rect[0] && rect[3] > rect[1]);
    assert(draw_mode == 0);
    if (rect[0] == 0 && rect[1] == 0 && rect[2] == 217 && rect[3] == 320)
        assert(draw_color[0] == (dark ? 24 : 255));
    if (rect[0] == 8 && rect[2] == 152)
        assert(draw_color[0] == (dark ? 26 : 237) &&
               draw_color[1] == (dark ? 46 : 245));
    for (int y = rect[0]; y < rect[2]; y++)
        for (int x = rect[1]; x < rect[3]; x++)
            memcpy(pixels[y][x], draw_color, 3);
    rectangles++;
}

int cfw_dark_mode_enabled(void)
{
    return dark;
}

static void *osos_resource_bank(void)
{
    return NULL;
}

static void *osos_resource_get(void *bank, uint32_t type, uint32_t id, uint32_t *size)
{
    (void)bank;
    assert(size);
    *size = 12;
    static uint32_t regular[] = {1, 58, 0}, bold[] = {1, 77, 1};
    assert(type == 0x464f4e54);
    assert(id == 0x0dad0020 || id == 0x0dad0000);
    return id == 0x0dad0020 ? regular : bold;
}

static uintptr_t osos_ui_string(uint32_t id)
{
    assert(id == 1);
    return (uintptr_t) "Helvetica";
}

static void osos_font_construct(uint32_t *font, const char *family, uint32_t size,
                                unsigned char style)
{
    assert(strcmp(family, "Helvetica") == 0);
    assert((size == 58 && style == 0) || (size == 77 && style == 1));
    font[0] = size;
    font[1] = 0;
}

static void osos_graphics_set_font(void *context, const uint32_t *font)
{
    (void)context;
    text_font = font[0];
}

static int osos_graphics_text_width(void *context, const char *text)
{
    (void)context;
    return (int)strlen(text) * (text_font == 58 ? 6 : 8);
}

static int osos_graphics_font_ascent(void *context)
{
    (void)context;
    return text_font == 58 ? 10 : 13;
}

static int osos_graphics_font_descent(void *context)
{
    (void)context;
    return text_font == 58 ? 3 : 4;
}

static void osos_graphics_set_pen(void *context, int x, int baseline)
{
    (void)context;
    pen_x = x;
    pen_y = baseline;
}

static void osos_graphics_set_background_color(void *context,
                                               const unsigned char *color)
{
    (void)context;
    assert(color[3] == 255);
}

static void osos_graphics_set_mode(void *context, unsigned char mode)
{
    (void)context;
    draw_mode = mode;
}

static void osos_graphics_draw_text(void *context, const char *text)
{
    assert(draw_mode == 0x21);
    assert(pen_x >= 0 && pen_x + osos_graphics_text_width(context, text) <= 320);
    assert(pen_y >= osos_graphics_font_ascent(context));
    assert(pen_y + osos_graphics_font_descent(context) <= 217);
    if (!strcmp(text, "EQ response"))
        assert(pen_x == 8);
    if (!strcmp(text, "Precut"))
        assert(pen_x + osos_graphics_text_width(context, text) == 312 - 36 - 6);
    if (!strcmp(text, "0.0 dB"))
        assert(pen_x + osos_graphics_text_width(context, text) == 312);
    text_draws++;
}

static void osos_wheel_decode(void *event)
{
    assert(((uint32_t *)event)[1] == 0x15);
    decoded++;
}

static uint32_t osos_button_ui_key(void *event)
{
    return (((uint32_t *)event)[4] >> 16) & 0xff;
}

static void *osos_wheel_accumulator_init(void *state, int threshold,
                                         unsigned char accelerate, uint32_t interval)
{
    assert(threshold == 5 && accelerate == 1 && interval == 350);
    assert(++wheel_constructions == 1);
    wheel_state = state;
    return state;
}

static void osos_wheel_accumulator_reset(void *state)
{
    assert(state == wheel_state);
    wheel_primed = 0;
    wheel_remainder = 0;
}

/* Native seam: model steady list quantization, not its clock or observer service. */
static int osos_wheel_accumulator_step(void *state, int motion, uint32_t speed)
{
    assert(state == wheel_state && speed == 0);
    if (!wheel_primed) {
        wheel_primed = 1;
        return 0;
    }
    int64_t total = (int64_t)wheel_remainder + motion;
    int steps = (int)(total / 5);
    wheel_remainder = (int)(total % 5);
    return steps;
}

static int osos_controller_handle_event(void *controller, void *event)
{
    (void)controller;
    (void)event;
    forwarded++;
    return 77;
}

static void osos_ui_invalidate(void)
{
    invalidated++;
}

static void *osos_controller_create_screen(const char *name, uint32_t screen,
                                           uint32_t layout, uint32_t transition)
{
    (void)name;
    (void)screen;
    (void)layout;
    (void)transition;
    return NULL;
}

static void *osos_controller_registry(void)
{
    return NULL;
}

static void osos_ui_register_controllers(void *registry, const char *name,
                                         void *ordinary,
                                         void *(*resource)(const char *, uint32_t,
                                                           uint32_t, uint32_t))
{
    (void)registry;
    (void)name;
    (void)ordinary;
    (void)resource;
}

#include "../../../payload/eq_coefficients.c"
#include "../../../payload/eq_canvas.c"

static int button(uint32_t kind, uint32_t code)
{
    /* UI key differs from the raw key and modifier fields. */
    uint32_t event[] = {0, kind, 0, 0, 0x010000a5 | (code << 16)};
    return canvas_event(NULL, event);
}

static void wheel(int delta)
{
    uint32_t event[] = {0, 0x15, 0, (uint32_t)delta, 0};
    assert(canvas_event(NULL, event) == 1);
}

int main(void)
{
    for (int i = 0; i < 49; i++)
        gain_labels[i] = "+12.0 dB";
    for (int i = 0; i < 32; i++)
        frequency_labels[i] = "20000 Hz";
    for (int i = 0; i < 38; i++)
        q_labels[i] = "4.0";
    for (int i = 0; i < CFW_EQ_BANDS; i++) {
        values[CFW_EQ_BAND1_GAIN + i * 4] = 24;
        values[CFW_EQ_BAND1_Q + i * 4] = 4;
    }
    frequency_labels[18] = "1000 Hz";
    frequency_labels[19] = "1250 Hz";
    frequency_labels[29] = "12500 Hz";
    char compact[12];
    values[CFW_EQ_BAND5_FREQUENCY] = 18;
    assert(!strcmp(canvas_frequency(4, compact), "1k Hz"));
    values[CFW_EQ_BAND5_FREQUENCY] = 19;
    assert(!strcmp(canvas_frequency(4, compact), "1.25k Hz"));
    values[CFW_EQ_BAND5_FREQUENCY] = 29;
    assert(!strcmp(canvas_frequency(4, compact), "12.5k Hz"));
    canvas_reset();
    assert(editor.mode == MODE_BAND && editor.band == 0);
    wheel(0);
    assert(decoded == 1 && invalidated == 0);
    assert(button(0x0d, 0x6e) == 77);
    assert(button(6, 0x6f) == 77);
    assert(editor.mode == MODE_BAND);
    wheel(2);
    wheel(2);
    assert(editor.band == 0);
    wheel(1);
    assert(editor.band == 1);
    wheel(2);
    wheel(2);
    assert(editor.band == 1);
    assert(button(0x0d, 0x6f) == 1);
    assert(editor.mode == MODE_PARAMETER);
    wheel(0);
    wheel(1);
    assert(editor.parameter == 0);
    wheel(14);
    assert(editor.parameter == PARAM_Q && editor.band == 1);
    assert(button(0x0d, 0x6f) == 1);
    wheel(0);
    wheel(5);
    assert(values[CFW_EQ_BAND2_Q] == 5 && changes == 1 && commits == 0);
    assert(values[CFW_EQ_BAND1_Q] == 4 && values[CFW_EQ_BAND3_Q] == 4);
    assert(button(0x0d, 0x6e) == 1);
    assert(editor.mode == MODE_PARAMETER);
    assert(button(0x0d, 0x6e) == 1);
    assert(editor.mode == MODE_BAND);
    assert(button(0x0d, 0x6e) == 77);
    assert(button(7, 0x6e) == 77);
    assert(button(0x0d, 0x10) == 77);
    assert(forwarded == 5);
    uint32_t raw_center[] = {0, 0x0d, 0, 0, 0x0010006f};
    assert(canvas_event(NULL, raw_center) == 77);
    assert(editor.mode == MODE_BAND);

    for (int mode = MODE_BAND; mode <= MODE_EDIT; mode++) {
        for (int band = 0; band < CFW_EQ_BANDS; band++) {
            for (int parameter = 0; parameter < PARAM_COUNT; parameter++) {
                canvas_reset();
                editor.mode = mode;
                editor.band = band;
                editor.parameter = parameter;
                wheel(0);
                wheel(INT_MAX);
                if (mode == MODE_BAND)
                    assert(editor.band == CFW_EQ_BANDS - 1);
                else if (mode == MODE_PARAMETER)
                    assert(editor.parameter == PARAM_COUNT - 1);
                else
                    assert(canvas_index(band, parameter) ==
                           canvas_field(band, parameter)->count - 1);
                canvas_draw(NULL, NULL, NULL);
                if (mode != MODE_BAND) {
                    int type = editor.parameter == PARAM_TYPE;
                    int left = type ? 0 : (editor.parameter - 1) * 320 / 3;
                    int right = type ? 319 : editor.parameter * 320 / 3 - 1;
                    int top = type ? 141 : 170;
                    int bottom = type ? 167 : 215;
                    assert(!memcmp(pixels[top][left], canvas_palettes[0][COLOR_ACCENT],
                                   3));
                    assert(!memcmp(pixels[top][right], canvas_palettes[0][COLOR_ACCENT],
                                   3));
                    assert(!memcmp(pixels[bottom][left + 3],
                                   canvas_palettes[0][COLOR_ACCENT], 3));
                }
                dark = 1;
                canvas_draw(NULL, NULL, NULL);
                dark = 0;
                wheel(INT_MIN);
                if (mode == MODE_BAND)
                    assert(editor.band == 0);
                else if (mode == MODE_PARAMETER)
                    assert(editor.parameter == 0);
                else
                    assert(canvas_index(band, parameter) == 0);
                canvas_draw(NULL, NULL, NULL);
            }
        }
    }
    canvas_reset();
    assert(editor.band == 0 && editor.parameter == 0 && editor.mode == MODE_BAND);
    values[CFW_EQ_BAND1_GAIN] = 32;
    canvas_reset();
    assert(canvas_index(0, PARAM_GAIN) ==
           32); /* Entry preserves persisted model values. */
    editor.parameter = PARAM_FREQUENCY;
    editor.mode = MODE_EDIT;
    unsigned before = commits;
    assert(canvas_activate(NULL, 1) == 1);
    cfw_eq_canvas_hold_changed(NULL, 1);
    assert(native_hold_calls == 1);
    assert(editor.response && commits == before + 1);
    unsigned previous_changes = changes;
    wheel(100);
    assert(button(0x0d, 0x6f) == 1);
    assert(changes == previous_changes && editor.mode == MODE_EDIT);
    canvas_draw(NULL, NULL, NULL);
    assert(snapshots == 1);
    assert(response_curve[130] != 0);
    dark = 1;
    canvas_draw(NULL, NULL, NULL);
    cfw_eq_canvas_hold_changed(NULL, 0);
    assert(native_hold_calls == 2);
    assert(!editor.response && editor.mode == MODE_EDIT &&
           editor.parameter == PARAM_FREQUENCY);
    wheel(0);
    uint32_t original = canvas_index(0, PARAM_FREQUENCY);
    rejected = 1;
    wheel(5);
    assert(canvas_index(0, PARAM_FREQUENCY) == original);
    rejected = 0;
    wheel(5);
    assert(canvas_index(0, PARAM_FREQUENCY) == original + 1);
    before = commits;
    assert(canvas_deactivate(NULL, 1) == 1 && commits == before + 1);
    canvas_draw(NULL, NULL, NULL);
    before = commits;
    cfw_eq_canvas_hold_changed(NULL, 1);
    assert(native_hold_calls == 3 && commits == before && !editor.response);
    assert(canvas_activate(NULL, 0) == 0 && editor.response);
    assert(editor.parameter == PARAM_FREQUENCY && editor.mode == MODE_EDIT);
    example_response = 1;
    version++;
    canvas_draw(NULL, NULL, NULL);
    assert(response_extent == 120);
    assert(response_curve[78] == 2); /* About 159 Hz, +0.2 dB after quantization. */
    assert(response_y(response_curve[78]) == 107);
    assert(!memcmp(pixels[107][44 + 78], canvas_palettes[1][COLOR_ACCENT], 3));
    for (int i = 0; i <= response_extent; i++)
        assert(response_y(i) + response_y(-i) == 216);
    assert(rectangles > 0);
    assert(text_draws > 0);
    puts("EQ canvas: model edits, Hold response, commit boundaries and drawing passed");
    return 0;
}
