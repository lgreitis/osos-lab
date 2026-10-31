/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"
#include "patch.h"
#include "ui.h"
#include "custom_eq.h"
#include "eq_coefficients.h"
#include "dark_mode.h"
#include "resources.h"

enum {
    CANVAS_TYPE = 0x63667763, /* cfwc */
    VIEW_BYTES = 0xa4,
    VIEW_OPTIONS_BYTES = 0x5c,
    VIEW_VTABLE_BYTES = 0x104,
    VIEW_VTABLE_PREFIX = 12,
    VIEW_DRAW_SLOT = 0x74,
};

enum {
    MODE_BAND,
    MODE_PARAMETER,
    MODE_EDIT,
};

enum {
    PARAM_TYPE,
    PARAM_GAIN,
    PARAM_FREQUENCY,
    PARAM_Q,
    PARAM_COUNT,
};

enum {
    CANVAS_WIDTH = 320,
    CANVAS_HEIGHT = 217,
    BAND_LEFT = 42,
    BAND_WIDTH = 54,
    BAND_TOP = 18,
    BAND_BOTTOM = 108,
    BAND_ZERO = (BAND_TOP + BAND_BOTTOM) / 2,
    TYPE_TOP = 141,
    TYPE_HEIGHT = 27,
    PARAM_TOP = TYPE_TOP + TYPE_HEIGHT + 1,
    PARAM_HEIGHT = CANVAS_HEIGHT - PARAM_TOP,
};

enum { ALIGN_LEFT = -1, ALIGN_CENTER, ALIGN_RIGHT };

static struct {
    int band, parameter, mode, response;
} editor;

static const unsigned int canvas_fields[] = {
    CFW_EQ_BAND1_TYPE,
    CFW_EQ_BAND1_GAIN,
    CFW_EQ_BAND1_FREQUENCY,
    CFW_EQ_BAND1_Q,
};

static const struct cfw_ui_field *canvas_field(int band, int parameter)
{
    return &cfw_eq_fields[canvas_fields[parameter] + band * 4];
}

static uint32_t canvas_index(int band, int parameter)
{
    const struct cfw_ui_field *field = canvas_field(band, parameter);
    return field->model->values[field->slot];
}

/* The native accumulator registers itself; keep its storage for the UI lifetime. */
static uint32_t canvas_wheel[0x34 / 4];
static int canvas_wheel_initialized;
static int canvas_active;

static void canvas_reset_wheel(void)
{
    if (!canvas_wheel_initialized) {
        osos_wheel_accumulator_init(canvas_wheel, 5, 1, 350);
        canvas_wheel_initialized = 1;
    }
    osos_wheel_accumulator_reset(canvas_wheel);
}

static void canvas_reset(void)
{
    editor.band = 0;
    editor.parameter = 0;
    editor.mode = MODE_BAND;
    editor.response = 0;
    canvas_reset_wheel();
}

static int canvas_clamp(int64_t value, int low, int high)
{
    return value < low ? low : value > high ? high : (int)value;
}

/* Hold events are consumed by the system UI before reaching screen controllers. */
PATCH_ARM void cfw_eq_canvas_hold_changed(void *system, int locked)
{
    if (canvas_active) {
        editor.response = !!locked;
        canvas_reset_wheel();
    }
    osos_system_hold_changed(system, locked);
    if (canvas_active) {
        cfw_eq_model.commit();
        osos_ui_invalidate();
    }
}

PATCH_CALL(0x0810af14, OSOS_SYSTEM_HOLD_CHANGED, cfw_eq_canvas_hold_changed);

static PATCH_ARM int canvas_event(void *controller, void *event)
{
    const uint32_t *words = event;
    if (editor.response && (words[1] == 0x15 || words[1] == 6 || words[1] == 0x0d))
        return 1;
    if (words[1] == 0x15) {
        osos_wheel_decode(event);
        /* Match native list input, including its steady five-count threshold. */
        int delta = osos_wheel_accumulator_step(canvas_wheel, (int32_t)words[3], 0);
        if (!delta)
            return 1;
        if (editor.mode == MODE_BAND) {
            editor.band =
                canvas_clamp((int64_t)editor.band + delta, 0, CFW_EQ_BANDS - 1);
        } else if (editor.mode == MODE_PARAMETER) {
            editor.parameter =
                canvas_clamp((int64_t)editor.parameter + delta, 0, PARAM_COUNT - 1);
        } else {
            const struct cfw_ui_field *field =
                canvas_field(editor.band, editor.parameter);
            uint32_t previous = field->model->values[field->slot];
            uint32_t value =
                canvas_clamp((int64_t)previous + delta, 0, field->count - 1);
            if (value != previous)
                field->model->change(field->slot, value);
        }
    } else if (words[1] == 0x0d && osos_button_ui_key(event) == 0x6f) {
        if (editor.mode == MODE_EDIT)
            cfw_eq_model.commit();
        /* Button release: Center enters controls, then toggles selection/editing. */
        editor.mode = editor.mode == MODE_PARAMETER ? MODE_EDIT : MODE_PARAMETER;
        canvas_reset_wheel();
    } else if (words[1] == 0x0d && osos_button_ui_key(event) == 0x6e &&
               editor.mode != MODE_BAND) {
        if (editor.mode == MODE_EDIT)
            cfw_eq_model.commit();
        editor.mode--;
        canvas_reset_wheel();
    } else {
        return osos_controller_handle_event(controller, event);
    }
    osos_ui_invalidate();
    return 1;
}

enum {
    COLOR_BACKGROUND,
    COLOR_ACCENT,
    COLOR_SELECTION,
    COLOR_TRACK,
    COLOR_INACTIVE,
    COLOR_GRID,
    COLOR_ZERO,
    COLOR_BORDER,
    COLOR_TEXT,
    COLOR_MUTED,
    COLOR_ON_ACCENT,
    COLOR_COUNT,
};

static const unsigned char canvas_palettes[2][COLOR_COUNT][4] = {
    {{255, 255, 255, 255},
     {20, 121, 213, 255},
     {237, 245, 252, 255},
     {238, 238, 238, 255},
     {143, 149, 154, 255},
     {231, 231, 231, 255},
     {181, 181, 181, 255},
     {214, 214, 214, 255},
     {32, 32, 32, 255},
     {85, 85, 85, 255},
     {255, 255, 255, 255}},
    {{24, 24, 24, 255},
     {0, 161, 255, 255},
     {26, 46, 63, 255},
     {44, 44, 44, 255},
     {125, 140, 152, 255},
     {48, 48, 48, 255},
     {100, 100, 100, 255},
     {60, 60, 60, 255},
     {232, 232, 232, 255},
     {168, 168, 168, 255},
     {16, 16, 16, 255}},
};

static uint32_t canvas_fonts[2][2];

static void canvas_load_fonts(void)
{
    /* Existing Legal body and Brightness value fonts, including native size/style. */
    static const uint32_t ids[] = {0x0dad0020, 0x0dad0000};
    for (unsigned int i = 0; i < 2; i++) {
        if (canvas_fonts[i][0] & ~1u)
            continue;
        uint32_t size = 0;
        const uint32_t *resource =
            osos_resource_get(osos_resource_bank(), 0x464f4e54, ids[i], &size);
        if (resource && size >= 12)
            osos_font_construct(canvas_fonts[i],
                                (const char *)osos_ui_string(resource[0]),
                                resource[1] & 0xffff, resource[2] & 0xff);
    }
}

static const char *canvas_value(int band, int parameter)
{
    const struct cfw_ui_field *field = canvas_field(band, parameter);
    return cfw_string(field->labels[canvas_index(band, parameter)]);
}

static void canvas_rect(void *context, const int32_t *bounds, int x, int y, int width,
                        int height, const unsigned char *color)
{
    int32_t rect[] = {bounds[0] + y, bounds[1] + x, bounds[0] + y + height,
                      bounds[1] + x + width};
    osos_graphics_set_mode(context, 0);
    osos_graphics_set_color(context, color);
    /* These colors already belong to the active theme. */
    osos_graphics_fill_rect_unmapped(context, rect);
}

static void canvas_text_aligned(void *context, const int32_t *bounds, int x, int y,
                                int width, int height, const char *text, int large,
                                int alignment, const unsigned char *color,
                                const unsigned char *background)
{
    if (!(canvas_fonts[large][0] & ~1u))
        return;
    osos_graphics_set_font(context, canvas_fonts[large]);
    int advance = osos_graphics_text_width(context, text);
    if (large && advance > width - 8 && (canvas_fonts[0][0] & ~1u)) {
        osos_graphics_set_font(context, canvas_fonts[0]);
        advance = osos_graphics_text_width(context, text);
    }
    int ascent = osos_graphics_font_ascent(context);
    int descent = osos_graphics_font_descent(context);
    int offset = alignment == ALIGN_LEFT    ? 0
                 : alignment == ALIGN_RIGHT ? width - advance
                                            : (width - advance) / 2;
    osos_graphics_set_pen(context, bounds[1] + x + offset,
                          bounds[0] + y + (height - ascent - descent) / 2 + ascent);
    osos_graphics_set_background_color(context, background);
    osos_graphics_set_color(context, color);
    osos_graphics_set_mode(context, 0x21);
    osos_graphics_draw_text(context, text);
}

static void canvas_text(void *context, const int32_t *bounds, int x, int y, int width,
                        int height, const char *text, int large,
                        const unsigned char *color, const unsigned char *background)
{
    canvas_text_aligned(context, bounds, x, y, width, height, text, large, ALIGN_CENTER,
                        color, background);
}

enum { RESPONSE_POINTS = 261 };

static int16_t response_curve[RESPONSE_POINTS];
static uint32_t response_version, response_sample_rate;
static int response_valid, response_extent;

static int response_y(int value)
{
    int offset =
        (value * 72 + (value < 0 ? -response_extent / 2 : response_extent / 2)) /
        response_extent;
    return 108 - offset;
}

static void canvas_response(void *context, const int32_t *bounds,
                            const unsigned char colors[COLOR_COUNT][4])
{
    int32_t coefficients[CFW_EQ_BANDS][5];
    uint32_t rate;
    uint32_t version = cfw_eq_snapshot(coefficients, &rate);
    if (!response_valid || version != response_version ||
        rate != response_sample_rate) {
        double hz = 20.0;
        response_extent = 60;
        for (unsigned int i = 0; i < RESPONSE_POINTS; i++) {
            int value = cfw_eq_response(coefficients, hz, rate);
            response_curve[i] = value;
            while (value < -response_extent || value > response_extent)
                response_extent *= 2;
            hz *= 1.0269243730546767;
        }
        response_version = version;
        response_sample_rate = rate;
        response_valid = 1;
    }
    canvas_text_aligned(context, bounds, 8, 3, 140, 24, "EQ response", 0, ALIGN_LEFT,
                        colors[COLOR_TEXT], colors[COLOR_BACKGROUND]);
    if (canvas_fonts[0][0] & ~1u) {
        const struct cfw_ui_field *precut = &cfw_eq_fields[CFW_EQ_PRECUT];
        const char *value = cfw_string(precut->labels[precut->model->values[precut->slot]]);
        osos_graphics_set_font(context, canvas_fonts[0]);
        int value_width = osos_graphics_text_width(context, value);
        int label_width = osos_graphics_text_width(context, "Precut");
        canvas_text_aligned(context, bounds, 312 - value_width - 6 - label_width, 3,
                            label_width, 24, "Precut", 0, ALIGN_LEFT,
                            colors[COLOR_MUTED], colors[COLOR_BACKGROUND]);
        canvas_text_aligned(context, bounds, 312 - value_width, 3, value_width, 24,
                            value, 0, ALIGN_RIGHT, colors[COLOR_TEXT],
                            colors[COLOR_BACKGROUND]);
    }
    static const int ticks[] = {44, 131, 217, 304};
    static const char *const labels[] = {"20", "200", "2k", "20k Hz"};
    for (unsigned int i = 0; i < 4; i++) {
        canvas_rect(context, bounds, ticks[i], 36, 1, 145, colors[COLOR_GRID]);
        int left = i == 3 ? 271 : ticks[i] - 21;
        canvas_text(context, bounds, left, 192, i == 3 ? 49 : 42, 22, labels[i], 0,
                    colors[COLOR_MUTED], colors[COLOR_BACKGROUND]);
    }
    canvas_rect(context, bounds, 44, 36, 261, 1, colors[COLOR_GRID]);
    canvas_rect(context, bounds, 44, 180, 261, 1, colors[COLOR_GRID]);
    canvas_rect(context, bounds, 44, 108, 261, 1, colors[COLOR_ZERO]);
    canvas_text(context, bounds, 0, 27, 38, 18,
                response_extent == 60    ? "+6"
                : response_extent == 120 ? "+12"
                : response_extent == 240 ? "+24"
                                         : "+48",
                0, colors[COLOR_MUTED], colors[COLOR_BACKGROUND]);
    canvas_text(context, bounds, 0, 99, 38, 18, "0", 0, colors[COLOR_MUTED],
                colors[COLOR_BACKGROUND]);
    canvas_text(context, bounds, 0, 171, 38, 18,
                response_extent == 60    ? "-6"
                : response_extent == 120 ? "-12"
                : response_extent == 240 ? "-24"
                                         : "-48",
                0, colors[COLOR_MUTED], colors[COLOR_BACKGROUND]);
    int previous = response_y(response_curve[0]);
    for (unsigned int i = 0; i < RESPONSE_POINTS; i++) {
        int y = response_y(response_curve[i]);
        int top = y < previous ? y : previous;
        int bottom = y > previous ? y : previous;
        canvas_rect(context, bounds, 44 + i, top, 1, bottom - top + 1,
                    colors[COLOR_ACCENT]);
        previous = y;
    }
}

static const char *canvas_frequency(int band, char text[12])
{
    const char *label = canvas_value(band, PARAM_FREQUENCY);
    unsigned int digits = 0;
    while (label[digits] >= '0' && label[digits] <= '9')
        digits++;
    if (digits < 4)
        return label;
    unsigned int end = digits;
    while (end > digits - 3 && label[end - 1] == '0')
        end--;
    unsigned int n = 0;
    for (unsigned int i = 0; i < end; i++) {
        if (i == digits - 3)
            text[n++] = '.';
        text[n++] = label[i];
    }
    text[n++] = 'k';
    text[n++] = ' ';
    text[n++] = 'H';
    text[n++] = 'z';
    text[n] = 0;
    return text;
}

static void canvas_bands(void *context, const int32_t *bounds,
                         const unsigned char colors[COLOR_COUNT][4])
{
    canvas_rect(context, bounds, BAND_LEFT, BAND_TOP, BAND_WIDTH * CFW_EQ_BANDS, 1,
                colors[COLOR_GRID]);
    canvas_rect(context, bounds, BAND_LEFT, BAND_BOTTOM, BAND_WIDTH * CFW_EQ_BANDS, 1,
                colors[COLOR_GRID]);
    canvas_rect(context, bounds, BAND_LEFT + 2 + editor.band * BAND_WIDTH, 4,
                BAND_WIDTH - 4, TYPE_TOP - 7, colors[COLOR_SELECTION]);
    canvas_rect(context, bounds, BAND_LEFT, BAND_ZERO, BAND_WIDTH * CFW_EQ_BANDS, 1,
                colors[COLOR_ZERO]);
    canvas_text(context, bounds, 2, BAND_TOP - 9, 34, 18, "+12", 0, colors[COLOR_MUTED],
                colors[COLOR_BACKGROUND]);
    canvas_text(context, bounds, 2, BAND_ZERO - 9, 34, 18, "0", 0, colors[COLOR_MUTED],
                colors[COLOR_BACKGROUND]);
    canvas_text(context, bounds, 2, BAND_BOTTOM - 9, 34, 18, "-12", 0,
                colors[COLOR_MUTED], colors[COLOR_BACKGROUND]);

    for (int band = 0; band < CFW_EQ_BANDS; band++) {
        int x = BAND_LEFT + BAND_WIDTH / 2 + band * BAND_WIDTH;
        char frequency[12];
        int y = BAND_ZERO - cfw_eq_gain[canvas_index(band, PARAM_GAIN)] *
                                (BAND_BOTTOM - BAND_ZERO) / 120;
        int selected = band == editor.band;
        int off = canvas_index(band, PARAM_TYPE) == CFW_EQ_OFF;
        const unsigned char *color = colors[off        ? COLOR_BORDER
                                            : selected ? COLOR_ACCENT
                                                       : COLOR_INACTIVE];
        canvas_rect(context, bounds, x - 8, BAND_TOP, 16, BAND_BOTTOM - BAND_TOP,
                    colors[COLOR_TRACK]);
        canvas_rect(context, bounds, x - 16, BAND_ZERO, 32, 1, colors[COLOR_ZERO]);
        int top = y < BAND_ZERO ? y : BAND_ZERO;
        int height = y < BAND_ZERO ? BAND_ZERO - y : y - BAND_ZERO;
        if (height)
            canvas_rect(context, bounds, x - 8, top, 16, height, color);
        canvas_rect(context, bounds, x - 12, y - 3, 24, 6, color);
        canvas_text(context, bounds, x - 25, TYPE_TOP - 25, 50, 22,
                    off ? "Off" : canvas_frequency(band, frequency), 0,
                    colors[selected ? COLOR_ACCENT : COLOR_MUTED],
                    colors[selected ? COLOR_SELECTION : COLOR_BACKGROUND]);
    }
}

static void canvas_parameters(void *context, const int32_t *bounds,
                              const unsigned char colors[COLOR_COUNT][4])
{
    canvas_rect(context, bounds, 0, TYPE_TOP - 1, CANVAS_WIDTH, 1,
                colors[COLOR_BORDER]);
    canvas_rect(context, bounds, 0, PARAM_TOP - 1, CANVAS_WIDTH, 1,
                colors[COLOR_BORDER]);
    canvas_rect(context, bounds, CANVAS_WIDTH / 3, PARAM_TOP, 1, PARAM_HEIGHT,
                colors[COLOR_BORDER]);
    canvas_rect(context, bounds, 2 * CANVAS_WIDTH / 3, PARAM_TOP, 1, PARAM_HEIGHT,
                colors[COLOR_BORDER]);
    for (int parameter = 0; parameter < PARAM_COUNT; parameter++) {
        int type = parameter == PARAM_TYPE;
        int left = type ? 0 : (parameter - 1) * CANVAS_WIDTH / 3;
        int width = type ? CANVAS_WIDTH : parameter * CANVAS_WIDTH / 3 - left;
        int top = type ? TYPE_TOP : PARAM_TOP;
        int height = type ? TYPE_HEIGHT : PARAM_HEIGHT;
        int active = editor.mode != MODE_BAND && parameter == editor.parameter;
        int editing = active && editor.mode == MODE_EDIT;
        const unsigned char *background = colors[editing  ? COLOR_ACCENT
                                                 : active ? COLOR_SELECTION
                                                          : COLOR_BACKGROUND];
        if (active) {
            canvas_rect(context, bounds, left, top, width, height,
                        colors[COLOR_ACCENT]);
            if (!editing)
                canvas_rect(context, bounds, left + 2, top + 2, width - 4, height - 4,
                            background);
        }
        const char *title = cfw_string(cfw_eq_fields[canvas_fields[parameter]].title);
        const unsigned char *label_color =
            colors[editing ? COLOR_ON_ACCENT : COLOR_MUTED];
        const unsigned char *value_color =
            colors[editing ? COLOR_ON_ACCENT : COLOR_TEXT];
        if (type) {
            canvas_text_aligned(context, bounds, 12, TYPE_TOP, 80, TYPE_HEIGHT, title,
                                0, ALIGN_LEFT, label_color, background);
            canvas_text_aligned(context, bounds, 100, TYPE_TOP, CANVAS_WIDTH - 112,
                                TYPE_HEIGHT, canvas_value(editor.band, parameter), 0,
                                ALIGN_RIGHT, value_color, background);
        } else {
            canvas_text(context, bounds, left, PARAM_TOP + 5, width, 16, title, 0,
                        label_color, background);
            canvas_text(context, bounds, left, PARAM_TOP + 21, width, 22,
                        canvas_value(editor.band, parameter), 1, value_color,
                        background);
        }
    }
}

static PATCH_ARM void canvas_draw(void *view, void *draw_argument, const int32_t *clip)
{
    (void)draw_argument;
    (void)clip;
    uint32_t context[0x44 / 4];
    int32_t bounds[4]; /* top, left, bottom, right; bottom/right are exclusive. */
    typedef void (*view_context_fn)(void *, void *);
    typedef void (*rect_fn)(void *, int32_t *);
    ((void *(*)(void *))OSOS_GRAPHICS_CONSTRUCT)(context);
    ((view_context_fn)OSOS_VIEW_BIND_GRAPHICS)(view, context);
    ((rect_fn)OSOS_VIEW_LOCAL_RECT)(view, bounds);
    const unsigned char(*colors)[4] = canvas_palettes[!!cfw_dark_mode_enabled()];
    canvas_rect(context, bounds, 0, 0, bounds[3] - bounds[1], bounds[2] - bounds[0],
                colors[COLOR_BACKGROUND]);
    if (bounds[2] - bounds[0] < CANVAS_HEIGHT || bounds[3] - bounds[1] < CANVAS_WIDTH)
        return;
    canvas_load_fonts();
    if (editor.response) {
        canvas_response(context, bounds, colors);
        return;
    }

    canvas_bands(context, bounds, colors);
    canvas_parameters(context, bounds, colors);
}

/* Preserve native destruction, attachment and layout methods plus ABI metadata. */
static const uintptr_t canvas_vtable[(VIEW_VTABLE_PREFIX + VIEW_VTABLE_BYTES) / 4] = {
    [(VIEW_VTABLE_PREFIX + VIEW_DRAW_SLOT) / 4] = (uintptr_t)canvas_draw,
};
PATCH_COPY(canvas_vtable, 0, OSOS_VIEW_VTABLE - VIEW_VTABLE_PREFIX,
           VIEW_VTABLE_PREFIX + VIEW_DRAW_SLOT);
PATCH_COPY(canvas_vtable, VIEW_VTABLE_PREFIX + VIEW_DRAW_SLOT + 4,
           OSOS_VIEW_VTABLE + VIEW_DRAW_SLOT + 4,
           VIEW_VTABLE_BYTES - VIEW_DRAW_SLOT - 4);

static PATCH_ARM void *canvas_create(void *owner, void *parent, void *event_target,
                                     const void *descriptor)
{
    (void)owner;
    uint32_t options[VIEW_OPTIONS_BYTES / 4];
    typedef void (*options_fn)(const void *, void *);
    typedef void *(*construct_fn)(void *, void *, void *, void *, const void *);
    for (unsigned int i = 0; i < VIEW_OPTIONS_BYTES / 4; i++)
        options[i] = 0;
    ((options_fn)OSOS_VIEW_INIT_OPTIONS)(descriptor, options);
    void *view = ((void *(*)(uint32_t))OSOS_CPP_NEW)(VIEW_BYTES);
    view = ((construct_fn)OSOS_VIEW_CONSTRUCT)(view, 0, parent, event_target, options);
    *(const uintptr_t **)view = canvas_vtable + VIEW_VTABLE_PREFIX / 4;
    return view;
}

static PATCH_ARM uint32_t canvas_activate(void *controller, uint32_t entering)
{
    editor.response = osos_hold_locked();
    canvas_reset_wheel();
    uint32_t result = osos_controller_activate(controller, entering);
    canvas_active = 1;
    return result;
}

static PATCH_ARM uint32_t canvas_deactivate(void *controller, uint32_t leaving)
{
    canvas_active = 0;
    cfw_eq_model.commit();
    return osos_controller_deactivate(controller, leaving);
}

static const uintptr_t canvas_controller_vtable[(12 + 0x128) / 4] = {
    [(12 + 0x30) / 4] = (uintptr_t)canvas_event,
    [(12 + 0xec) / 4] = (uintptr_t)canvas_activate,
    [(12 + 0xf0) / 4] = (uintptr_t)canvas_deactivate,
};
PATCH_COPY(canvas_controller_vtable, 0, OSOS_CONTROLLER_SCREEN_VTABLE - 12, 12 + 0x30);
PATCH_COPY(canvas_controller_vtable, 12 + 0x34, OSOS_CONTROLLER_SCREEN_VTABLE + 0x34,
           0xec - 0x34);
PATCH_COPY(canvas_controller_vtable, 12 + 0xf4, OSOS_CONTROLLER_SCREEN_VTABLE + 0xf4,
           0x128 - 0xf4);

static PATCH_ARM void *canvas_controller_create(const char *name, uint32_t screen,
                                                uint32_t layout, uint32_t transition)
{
    canvas_reset();
    void *controller = osos_controller_create_screen(name, screen, layout, transition);
    *(const uintptr_t **)controller = canvas_controller_vtable + 3;
    return controller;
}

void cfw_ui_register_canvas(void)
{
    typedef void (*register_fn)(uint32_t, void *);
    ((register_fn)OSOS_REGISTER_VIEW_FACTORY)(CANVAS_TYPE, canvas_create);
    osos_ui_register_controllers(osos_controller_registry(), "CFWCanvas", 0,
                                 canvas_controller_create);
}
