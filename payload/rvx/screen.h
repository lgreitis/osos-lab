/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "internal.h"

struct context {
    Pkg *self;
    void *controller, *arg;
    const void *item;
    int32_t row;
    void *list;
};

struct row {
    const char *label;
    uint8_t kind, reserved;
    uint16_t key;

    union {
        const char *screen;
        u32 stock;
    };

    void (*action)(const struct context *);
    const char *(*text)(const struct context *);
    const char *id;
};

struct context_row {
    struct row row;
    int (*available)(const struct context *);
};
struct bitmap;

struct pane {
    const char *label;
    const struct bitmap *bitmap;
    const char *(*text)(const struct context *);
    const struct bitmap *(*image)(const struct context *);
    uint8_t plain;
};

struct pane_extra {
    const char *lines[2];
    const char *(*text)(const struct context *, u32);
    const struct bitmap *badge;
    const struct bitmap *(*image)(const struct context *);
};

struct menu_entry {
    uint16_t menus, shown;
    u32 after;
    struct row row;
    const struct pane *pane;
    const struct pane_extra *extra;
    int (*available)(const struct context *);
    uint16_t caption;
    uint8_t preview, plain;
    const char *host, *host_screen, *host_after;
};

struct input {
    uint8_t kind, button;
    int16_t delta;
};

struct rect {
    int16_t x, y, w, h;
};

struct bitmap {
    const char *name;
};

struct graphics {
    u32 size;
    u32 (*color)(uint8_t);
    void (*fill)(const struct rect *, u32);
    void (*text)(int, int, uint8_t, u32, const char *);
    int (*measure)(uint8_t, const char *), (*line_height)(uint8_t);
    void (*bitmap)(int, int, const struct bitmap *), (*bmap)(int, int, const void *);
    int (*paragraph)(int, int, int, uint8_t, u32, const char *, u32);
    void (*row)(int, int, const char *, const char *, int);
    void (*rows)(int, int, const void *, int, u32),
        (*blend)(int, int, const void *, int, uint8_t);
    void (*tile)(int, int, int, const void *);
    void (*card)(int, int, const char *, const char *, const struct bitmap *);
    void (*text_in)(const struct rect *, uint8_t, u32, const char *, int8_t);
    u32 (*mix)(u32, u32, uint8_t);
    const struct rect *(*clip)(void);
    void (*bar)(const struct rect *);
};

struct screen {
    const char *id, *title;
    uint8_t kind, layout, transition, reserved;

    union {
        struct {
            const struct row *rows;
            u32 count;
            const struct pane *panes;
        } list;

        struct {
            uint16_t key, reserved;
            const char *const *labels;
            u32 count;
        } choice;

        struct {
            const char *const *lines;
            u32 count;
        } text;

        struct {
            void (*draw)(const struct context *, const struct graphics *, int, int);
            int (*input)(const struct context *, const struct input *);
        } canvas;

        struct {
            u32 (*count)(const struct context *);
            const char *(*row)(const struct context *, u32, const char **);
            int (*select)(const struct context *, u32);
            u32 (*initial)(const struct context *);
            uint8_t detail;
            int (*status)(const struct context *, u32);
            const char *empty;
        } dynamic;

        struct {
            uint8_t screen;
        } stock;

        struct {
            const char *message, *detail;
            const char *const *buttons;
            uint8_t count, initial;
            int (*chosen)(const struct context *, u32);
        } dialog;
    };

    const char *caption;
    const struct bitmap *bitmap;
    void (*opened)(const struct context *), (*closed)(const struct context *);
    const struct context_row *context_rows;
    u32 context_count;
};

struct frame {
    struct frame *parent;
    struct context ctx;
    const struct screen *screen;
    struct screen setting_screen;
    u32 selected, module;
    u32 media_item[6];
    struct core_screen *native;
    char (*context_actions)[11];
    int32_t range[3];
    void *view;
    struct cached_bitmap *bitmaps;
    uintptr_t *stock_methods;
    const uintptr_t *native_methods;
    struct rect damage;
    int consumed, held_button, split;
};
extern struct frame *rvx_top;
extern void *rvx_bank;
/* NavigatorAction_PopToScreen filter: a pop to the Settings menu ends on the
 * newest open list holding a moved Settings row. */
u32 rvx_pop_to_screen(u32 screen, u32 push_history, u32 skip_history, const void *next);
extern const struct graphics rvx_graphics;
void rvx_register_controller(void);
void rvx_begin_draw(void *context, const int32_t *bounds, const struct rect *dirty);
int rvx_prepare_native(struct frame *, u32 *screen, u32 *layout);
/* The shared canvas screen and its layout; 0 when it cannot be built. */
int rvx_prepare_canvas(u32 *screen, u32 *layout);
/* The property the canvas screen's title is bound to, 0 until it is built. */
u32 rvx_canvas_title(void);
int rvx_native_property(u32, u32, uintptr_t *);
int rvx_dialog_valid(const struct screen *);
void rvx_dialog_attach(struct frame *);
int rvx_native_action(void *, const char *, u32);
void rvx_native_refresh(struct frame *);
u32 rvx_row_count(struct frame *);
const char *rvx_setting_label(Pkg *, uint16_t);
const void *rvx_frame_bitmap(struct frame *, const struct bitmap *, u32 *size);
int rvx_is_pane_icon(const u32 *image, u32 bytes);
int rvx_push(Pkg *, const char *, void *);
void rvx_pop(Pkg *);
void rvx_select(Pkg *, const char *, u32);
int rvx_colors(Pkg *, const u32 *);
int rvx_provider(Pkg *, const void *);
void rvx_act(const struct context *, const struct row *);
extern const struct context *rvx_origin;
const struct rvx_setting *rvx_declaration(Pkg *, uint16_t);

void rvx_refresh_pane(Pkg *);
void rvx_clear_bitmaps(struct frame *);
void rvx_theme_changed(void);
