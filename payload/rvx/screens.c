/* SPDX-License-Identifier: GPL-3.0-only */
#include "native_ui.h"
#include "cfw.h"
#include "osos.h"
#include "patch.h"

struct frame *rvx_top;
const struct context *rvx_origin;
/* Wheel motion per step, as the stock menu lists count it: their MenuView
 * overrides the accumulator's threshold with 4 for an ordinary row height and
 * raises the multiplier ceiling to 32. */
enum { WHEEL_THRESHOLD = 4, WHEEL_INTERVAL = 350, WHEEL_MAX_MULTIPLIER = 32 };
static u32 wheel[WheelAccumulator_bytes / 4];
/* The buttons whose press the screen on top consumed, so their release is its
 * too, and the screen that saw them. */
static uint8_t pressed;
static const struct frame *pressed_frame;

/* The top frame, not counting one being popped. */
struct frame *rvx_shown(void)
{
    struct frame *f = rvx_top;
    while (f && f->leaving)
        f = f->parent;
    return f;
}

static void refresh_title(void)
{
    void *model = ((void *(*)(void))0x081e0ff4)();
    u32 title = rvx_canvas_title();
    if (model && title)
        osos_model_notify(model, OSOS_NOTIFY_ALL, title);
}

static PATCH_ARM u32 destroy(void *controller, u32 mode);
static struct frame *lookup(void *controller);
static PATCH_ARM int event(void *controller, void *e);

/* A slider or canvas slides in as Settings > About does, through its
 * template's styles; from a split list it may instead start at the pane, as
 * Extras > Stopwatch does, with Stopwatch's own style. Over another full
 * screen the two share the template, which then would not move, so the whole
 * screen slides. */
static int slide_in(struct frame *f, u32 layout, enum core_template template,
                    uint8_t from)
{
    int pane = f->screen->transition == RV_TRANSITION_FROM_PANE &&
               from == LayoutKind_SplitList;
    f->style = pane ? core_templates_enter_style(RES_Stopwatch_Template,
                                                 LayoutKind_SplitList)
                    : -1;
    core_clone_set_style(layout, core_template(template), f->style);
    return from == LayoutKind_FullScreen ? Transition_ScreenPush : Transition_Standard;
}

/* The layout a list screen opens in from `controller`'s screen: what it
 * declares, else its parent's: a split list of the runtime or one of Apple's
 * Settings lists (the Settings menu and its submenus share one class) gives
 * split, anything else full width. */
static int split_from(const struct screen *s, void *controller)
{
    if (s->layout != RV_LAYOUT_AUTO)
        return s->layout == RV_LAYOUT_SPLIT;
    const struct frame *parent = lookup(controller);
    if (parent)
        return core_lists_kind(parent->screen->kind) && parent->split;
    return RV_FIELD(controller, 0, const void *) ==
           (const void *)ADDR_SettingsMenuListController_vtable;
}

/* The controller on top of the navigation stack. */
static void *top_controller(void)
{
    void *nav = ((void *(*)(void))ADDR_Navigator_Get)();
    return nav ? RV_FIELD(nav, Navigator_top_controller, void *) : NULL;
}

static void select_row(struct frame *f, u32 row)
{
    u32 rows = rvx_row_count(f);
    if (!rows)
        return;
    f->selected = row < rows ? row : rows - 1;
    osos_ui_list_select(f->selected);
}

/* A list that shows again takes the row ui_select asked for while it was
 * covered; the stock activation restores its saved row first. */
static PATCH_ARM u32 activate(void *controller, u32 entering)
{
    struct frame *f = lookup(controller);
    typedef u32 (*Activate)(void *, u32);
    Activate stock =
        f ? (Activate)f->native_methods[ScreenController_activate_method / 4] : NULL;
    u32 result = stock ? stock(controller, entering) : 0;
    f = f ? lookup(controller) : NULL;
    if (f && f->select_pending) {
        f->select_pending = 0;
        select_row(f, f->selected);
    }
    return result;
}

static int present(struct frame *f, void *parent)
{
    if (!parent) {
        void *nav = ((void *(*)(void))0x08104d00)();
        parent = nav ? *(void **)((uint8_t *)nav + 4) : NULL;
    }
    if (!parent) {
        osos_import_free(f);
        return NOT_FOUND;
    }
    f->parent = rvx_top;
    uint8_t from =
        RV_FIELD(NATIVE_CALL(UI_GetLayoutManager)(), LayoutManager_kind, uint8_t);
    f->split = core_lists_kind(f->screen->kind) && split_from(f->screen, parent);
    int native = f->screen->kind != RV_SCREEN_KIND_CANVAS &&
                 f->screen->kind != RV_SCREEN_KIND_STOCK;
    int dialog = f->screen->kind == RV_SCREEN_KIND_DIALOG;
    if (dialog && !rvx_dialog_valid(f->screen)) {
        osos_import_free(f);
        return BAD_ARGUMENT;
    }
    if (f->screen->kind == RV_SCREEN_KIND_CHOICE ||
        f->screen->kind == RV_SCREEN_KIND_SLIDER) {
        u32 value = rvx_get_int(f->ctx.self, f->screen->choice.key);
        f->selected = value < f->screen->choice.count ? value : 0;
    }
    osos_wheel_accumulator_init(wheel, WHEEL_THRESHOLD, 1, WHEEL_INTERVAL);
    typedef void (*SetMax)(void *, int);
    ((SetMax)ADDR_WheelAccumulator_SetMaxMultiplier)(wheel, WHEEL_MAX_MULTIPLIER);
    void *c = NULL;
    if (native || f->screen->kind == RV_SCREEN_KIND_STOCK) {
        if (!native && f->screen->stock.screen != RV_STOCK_SCREEN_GAMES) {
            osos_import_free(f);
            return NOT_FOUND;
        }
        f->stock_methods = osos_import_alloc(12 + 0x200);
        if (f->stock_methods) {
            u32 screen, layout;
            if (!rvx_prepare_native(f, &screen, &layout)) {
                osos_import_free(f->stock_methods);
                osos_import_free(f);
                return NO_MEMORY;
            }
            rvx_top = f;
            /* A dialog comes up over the screen as the stock EU Volume Limit
             * question does. */
            int transition = dialog     ? Transition_ForegroundPush
                             : f->split ? Transition_Standard
                                        : Transition_ScreenPush;
            if (f->screen->kind == RV_SCREEN_KIND_SLIDER)
                transition = slide_in(f, layout, CORE_TEMPLATE_SLIDER, from);
            c = osos_create_screen_controller(osos_controller_registry(), screen,
                                              layout, transition);
            if (c) {
                f->native_methods = *(const uintptr_t **)c;
                for (unsigned i = 0; i < (12 + 0x200) / 4; i++)
                    f->stock_methods[i] = (f->native_methods - 3)[i];
                f->stock_methods[4] = (uintptr_t)destroy;
                if (native && f->screen->kind != RV_SCREEN_KIND_TEXT &&
                    f->screen->kind != RV_SCREEN_KIND_SLIDER && !dialog)
                    f->stock_methods[3 + ScreenController_action_method / 4] =
                        (uintptr_t)rvx_native_action;
                if (f->screen->kind == RV_SCREEN_KIND_SLIDER)
                    f->stock_methods[3 + ScreenController_event_method / 4] =
                        (uintptr_t)event;
                if (core_lists_kind(f->screen->kind))
                    f->stock_methods[3 + ScreenController_activate_method / 4] =
                        (uintptr_t)activate;
                if (dialog)
                    rvx_dialog_attach(f);
                *(uintptr_t **)c = f->stock_methods + 3;
            } else {
                rvx_top = f->parent;
                osos_import_free(f->stock_methods);
            }
        }
    } else {
        u32 screen, layout;
        rvx_top = f;
        if (rvx_prepare_canvas(&screen, &layout))
            c = osos_create_screen_controller(
                osos_controller_registry(), screen, layout,
                slide_in(f, layout, CORE_TEMPLATE_CANVAS, from));
    }
    if (!c) {
        rvx_top = f->parent;
        rvx_clear_bitmaps(f);
        osos_import_free(f);
        return NO_MEMORY;
    }
    f->ctx.controller = c;
    rvx_top = f;
    if (f->screen->kind == RV_SCREEN_KIND_DYNAMIC_LIST && f->screen->dynamic.initial)
        f->selected = f->screen->dynamic.initial(&f->ctx);
    osos_controller_push(parent, c);
    if (!f->stock_methods)
        refresh_title();
    if ((f->screen->kind == RV_SCREEN_KIND_CHOICE ||
         (f->screen->kind == RV_SCREEN_KIND_DYNAMIC_LIST &&
          f->screen->dynamic.initial)) &&
        f->selected < rvx_row_count(f))
        osos_ui_list_select(f->selected);
    if (f->screen->opened)
        f->screen->opened(&f->ctx);
    return OK;
}

static struct frame *frame(Pkg *p, void *arg)
{
    struct frame *f = osos_import_alloc(sizeof *f);
    if (f) {
        if (rvx_origin)
            f->ctx = *rvx_origin;
        else if (rvx_top)
            f->ctx = rvx_top->ctx;
        else
            f->ctx.row = -1;
        if (f->ctx.item) {
            for (unsigned i = 0; i < 6; i++)
                f->media_item[i] = ((const u32 *)f->ctx.item)[i];
            f->ctx.item = f->media_item;
        }
        f->ctx.self = p;
        f->ctx.arg = arg;
    }
    return f;
}

static int open_id(Pkg *p, const char *id, void *arg, void *controller)
{
    if (!id)
        return BAD_ARGUMENT;
    if (!p) {
        for (u32 j = 0; j < rvx_core_screen_count; j++) {
            if (!cfw_string_equal(rvx_core_screens[j].id, id))
                continue;
            struct frame *f = frame(NULL, arg);
            if (!f)
                return NO_MEMORY;
            f->module = 0;
            f->screen = rvx_core_screens + j;
            return present(f, controller);
        }
        return NOT_FOUND;
    }
    for (unsigned i = 0; i < p->manifest->modules; i++) {
        struct rvx_module *m = p->modules + i;
        if (!m->image && p->records[i].trigger == 1) {
            const char *s = p->strings + p->records[i].first_screen;
            for (unsigned j = 0; j < p->records[i].screens; j++) {
                if (cfw_string_equal(s, id)) {
                    if (rvx_load_module(p, i) != OK)
                        return NOT_FOUND;
                    break;
                }
                while (*s++) {
                }
            }
        }
        if (!m->desc)
            continue;
        const struct screen *screens = m->desc->screens;
        for (u32 j = 0; j < m->desc->screen_count; j++)
            if (cfw_string_equal(screens[j].id, id)) {
                struct frame *f = frame(p, arg);
                if (!f)
                    return NO_MEMORY;
                f->module = i;
                f->screen = screens + j;
                return present(f, controller);
            }
    }
    return NOT_FOUND;
}

int rvx_push(Pkg *p, const char *id, void *arg)
{
    return open_id(p, id, arg, rvx_origin ? rvx_origin->controller : NULL);
}

void rvx_pop(Pkg *p)
{
    struct frame *f = rvx_shown();
    if (f && f->ctx.self == p)
        ((int (*)(void *, u32))0x081034f0)(f->ctx.controller, 0);
}

/* ui_select: a dynamic list's row, clamped to its count; on a covered list it
 * is applied when the list shows again. */
void rvx_select(Pkg *p, const char *id, u32 row)
{
    for (struct frame *f = rvx_top; f; f = f->parent)
        if (f->ctx.self == p && id && f->screen->kind == RV_SCREEN_KIND_DYNAMIC_LIST &&
            cfw_string_equal(f->screen->id, id)) {
            if (f->ctx.controller == top_controller()) {
                select_row(f, row);
            } else {
                f->selected = row;
                f->select_pending = 1;
            }
            break;
        }
}

/* Apple's Volume Limit control and lock-screen layouts leave with a pop to
 * the Settings menu, which is below the list holding the moved row. */
u32 rvx_pop_to_screen(u32 screen, u32 push_history, u32 skip_history, const void *next)
{
    for (struct frame *f = rvx_top; screen == RES_SettingsMenu_Main_Screen && f;
         f = f->parent)
        if (f->native && f->screen->kind == RV_SCREEN_KIND_LIST &&
            core_lists_stock_count(f->screen)) {
            screen = core_lists_screen_id(f->native, f->split);
            break;
        }
    return rvx_next(next, screen, push_history, skip_history, 0, 0, 0);
}

/* A declared choice, multi-choice or slider screen for the setting, with its
 * own title and icon, instead of the one the core would make from the setting. */
static const struct screen *declared_setting_screen(Pkg *p, uint16_t key,
                                                    unsigned *module)
{
    for (unsigned i = 0; i < p->manifest->modules; i++) {
        const struct rvx_module *m = p->modules + i;
        if (!m->desc)
            continue;
        const struct screen *screens = m->desc->screens;
        for (u32 j = 0; j < m->desc->screen_count; j++) {
            const struct screen *s = screens + j;
            if ((s->kind == RV_SCREEN_KIND_CHOICE ||
                 s->kind == RV_SCREEN_KIND_MULTI_CHOICE ||
                 s->kind == RV_SCREEN_KIND_SLIDER) &&
                s->choice.key == key) {
                *module = i;
                return s;
            }
        }
    }
    return NULL;
}

/* Runs a row's selection; for a row that opens a screen, whether it did. */
int rvx_act(const struct context *ctx, const struct row *r)
{
    Pkg *p = ctx->self;
    switch (r->kind) {
    case 0:
        return open_id(p, r->screen, NULL, ctx->controller);
    case 1:
        if (r->action)
            r->action(ctx);
        break;
    case 2: {
        /* A choice, multi-choice or slider screen of the setting opens: a
         * declared one, else the one a labelled int with a screen kind makes.
         * Otherwise Select steps a labelled int to its next label, as stock
         * Shuffle and Repeat do, and flips anything else. */
        const struct rvx_setting *s = rvx_declaration(p, r->key);
        unsigned module = 0;
        const struct screen *declared = declared_setting_screen(p, r->key, &module);
        int generated = s && s->type == RV_SETTING_TYPE_INT && s->labels &&
                        s->label_count && s->screen;
        if (!declared && !generated) {
            int32_t v = rvx_get_int(p, r->key);
            if (s && s->labels && s->label_count) {
                if ((u32)v >= s->label_count)
                    v = s->fallback; /* the label the row shows */
                v = (u32)v + 1 >= s->label_count ? 0 : v + 1;
            } else {
                v = !v;
            }
            rvx_write_setting(p, r->key, RV_SETTING_TYPE_INT, &v, sizeof v);
        } else {
            struct frame *f = frame(p, NULL);
            if (!f)
                break;
            if (declared) {
                f->module = module;
                f->screen = declared;
            } else {
                f->screen = &f->setting_screen;
                f->setting_screen.title = s->label;
                f->setting_screen.kind = s->screen;
                f->setting_screen.choice.key = r->key;
                f->setting_screen.choice.labels = s->labels;
                f->setting_screen.choice.count = s->label_count;
            }
            present(f, ctx->controller);
        }
        osos_ui_invalidate();
        break;
    }
    }
    return OK;
}

/* A setting's value as text: its label, the fallback's label for a value past
 * the labels, On or Off without labels, NULL for a multi-choice. */
const char *rvx_setting_label(Pkg *p, uint16_t key)
{
    int32_t n = rvx_get_int(p, key);
    const struct rvx_setting *s = rvx_declaration(p, key);
    if (!s || !s->labels)
        return n ? "On" : "Off";
    if (s->screen == RV_SCREEN_KIND_MULTI_CHOICE)
        return NULL;
    if ((u32)n >= s->label_count)
        n = s->fallback;
    return (u32)n < s->label_count ? s->labels[n] : NULL;
}

static void slide(struct frame *f, int steps)
{
    const struct screen *s = f->screen;
    int value = rvx_get_int(f->ctx.self, s->choice.key);
    int last = (int)s->choice.count - 1;
    if (last < 0)
        return;
    value = value < 0 ? 0 : value > last ? last : value;
    int next = value + steps < 0 ? 0 : value + steps > last ? last : value + steps;
    if (next != value)
        rvx_set_int(f->ctx.self, s->choice.key, next);
}

static int key_of(u32 key)
{
    switch (key) {
    case UIKey_Center:
        return RV_BUTTON_CENTER;
    case UIKey_Menu:
        return RV_BUTTON_MENU;
    case UIKey_Previous:
        return RV_BUTTON_PREVIOUS;
    case UIKey_Next:
        return RV_BUTTON_NEXT;
    case UIKey_Play:
        return RV_BUTTON_PLAY;
    default:
        return -1;
    }
}

/* 1 when the screen consumed the event; what it leaves goes to Apple's
 * handler, which runs the layout's bindings (Menu pops). A release goes where
 * its press went. */
static int rvx_input(struct frame *f, u32 kind, void *e)
{
    const struct screen *s = f->screen;
    struct input in = {0};
    if (kind == InputEvent_Wheel) {
        u32 speed = ((u32(*)(void *))ADDR_WheelEvent_GetSpeed)(e);
        osos_wheel_decode(e);
        int steps = osos_wheel_accumulator_step(
            wheel, RV_FIELD(e, InputEvent_wheel_delta, int32_t), speed);
        if (!steps)
            return 1;
        /* One detent moves one step, as a stock list does, unless the wheel
         * turns fast enough to scroll by more. */
        if (((int (*)(void *, u32))ADDR_WheelAccumulator_IsSlowTurn)(wheel, speed))
            steps = steps > 0 ? 1 : -1;
        if (s->kind == RV_SCREEN_KIND_SLIDER) {
            slide(f, steps);
            return 1;
        }
        in.kind = RV_INPUT_WHEEL;
        in.delta = (int16_t)steps;
    } else if (kind == InputEvent_ButtonDown || kind == InputEvent_ButtonUp ||
               kind == InputEvent_ButtonPressAndHold) {
        int key = key_of(osos_button_ui_key(e));
        /* A held Menu is the system's way to the main menu. */
        if (key < 0 || (kind == InputEvent_ButtonPressAndHold && key == RV_BUTTON_MENU))
            return 0;
        if (s->kind == RV_SCREEN_KIND_SLIDER) {
            if ((key != RV_BUTTON_PREVIOUS && key != RV_BUTTON_NEXT) ||
                kind == InputEvent_ButtonPressAndHold)
                return 0;
            if (kind == InputEvent_ButtonDown)
                slide(f, key == RV_BUTTON_NEXT ? 1 : -1);
            return 1;
        }
        in.kind = kind == InputEvent_ButtonDown           ? RV_INPUT_PRESS
                  : kind == InputEvent_ButtonPressAndHold ? RV_INPUT_LONG_PRESS
                                                          : RV_INPUT_RELEASE;
        in.button = (uint8_t)key;
    } else {
        return 0;
    }
    void *controller = f->ctx.controller;
    int consumed = s->kind == RV_SCREEN_KIND_CANVAS && s->canvas.input &&
                   s->canvas.input(&f->ctx, &in);
    if (lookup(controller) != f) /* the callback closed the screen */
        return 1;
    if (in.kind == RV_INPUT_WHEEL || in.kind == RV_INPUT_LONG_PRESS)
        return consumed;
    uint8_t bit = (uint8_t)(1u << in.button);
    if (f != pressed_frame) {
        pressed = 0;
        pressed_frame = f;
    }
    if (in.kind == RV_INPUT_PRESS) {
        pressed = consumed ? pressed | bit : pressed & ~bit;
        return consumed;
    }
    consumed = (pressed & bit) != 0;
    pressed &= ~bit;
    return consumed;
}

static struct frame *lookup(void *controller)
{
    for (struct frame *f = rvx_top; f; f = f->parent)
        if (f->ctx.controller == controller)
            return f;
    return NULL;
}

static PATCH_ARM int event(void *controller, void *e)
{
    struct frame *f = lookup(controller);
    if (f && rvx_input(f, RV_FIELD(e, InputEvent_kind, u32), e))
        return 1; /* also when the callback closed the screen */
    if (f && f->native_methods) {
        typedef int (*Event)(void *, void *);
        return ((Event)f->native_methods[ScreenController_event_method / 4])(controller,
                                                                             e);
    }
    return osos_controller_handle_event(controller, e);
}

static PATCH_ARM void draw(void *view, void *arg, const int32_t *clip)
{
    /* Canvas screens share the template's view: it draws the canvas on top,
     * not counting one being popped. Under another kind of screen it draws
     * the canvas it last drew, which slides out after a pop. */
    struct frame *top = rvx_shown();
    struct frame *f = top && top->screen->kind == RV_SCREEN_KIND_CANVAS ? top : NULL;
    if (f)
        f->view = view;
    else
        for (f = rvx_top; f && f->view != view; f = f->parent) {
        }
    if (!f || !f->screen->canvas.draw) {
        typedef void (*Draw)(void *, void *, const int32_t *);
        ((const Draw *)OSOS_VIEW_VTABLE)[View_draw_method / 4](view, arg, clip);
        return;
    }
    u32 context[GraphicsContext_bytes / 4];
    int32_t bounds[4];
    osos_graphics_construct(context);
    osos_view_bind_graphics(view, context);
    osos_view_local_rect(view, bounds);
    rvx_begin_draw(context, bounds);
    f->screen->canvas.draw(&f->ctx, &rvx_graphics, bounds[3] - bounds[1],
                           bounds[2] - bounds[0]);
}

static uintptr_t view_table[(12 + 0x104) / 4], controller_table[(12 + 0x128) / 4];

static PATCH_ARM void *create_view(void *owner, void *parent, void *target,
                                   const void *desc)
{
    (void)owner;
    u32 opts[0x5c / 4];
    for (unsigned i = 0; i < sizeof opts / 4; i++)
        opts[i] = 0;
    osos_view_init_options(desc, opts);
    void *v = osos_view_construct(osos_cpp_new(0xa4), parent, target, opts);
    if (v)
        *(uintptr_t **)v = view_table + 3;
    return v;
}

static PATCH_ARM u32 destroy(void *controller, u32 mode)
{
    struct frame *f = lookup(controller);
    typedef u32 (*Destroy)(void *, u32);
    Destroy orig = ((const Destroy *)OSOS_CONTROLLER_SCREEN_VTABLE)[1];
    if (f && f->stock_methods)
        orig = (Destroy)f->native_methods[1];
    if (f && f->screen->closed)
        f->screen->closed(&f->ctx);
    u32 result = orig(controller, mode);
    if (f) {
        struct frame **link = &rvx_top;
        while (*link && *link != f)
            link = &(*link)->parent;
        if (*link)
            *link = f->parent;
        if (pressed_frame == f)
            pressed_frame = NULL;
        if (rvx_top && rvx_top->screen->kind == RV_SCREEN_KIND_CANVAS)
            refresh_title();
        if (f->stock_methods)
            osos_import_free(f->stock_methods);
        Pkg *p = f->ctx.self;
        unsigned module = f->module;
        if (f->context_actions)
            osos_import_free(f->context_actions);
        rvx_clear_bitmaps(f);
        osos_import_free(f);
        if (module &&
            (p->records[module].trigger == 1 || p->modules[module].pending_unload) &&
            p->records[module].preload != 0) {
            for (f = rvx_top; f; f = f->parent)
                if (f->ctx.self == p && f->module == module)
                    return result;
            rvx_release_screen(p, module);
        }
    }
    return result;
}

/* A canvas shown again gives the shared canvas layout its own style back,
 * whatever screen covered it. */
static PATCH_ARM u32 canvas_activate(void *controller, u32 entering)
{
    typedef u32 (*Activate)(void *, u32);
    const Activate *stock = (const Activate *)OSOS_CONTROLLER_SCREEN_VTABLE;
    struct frame *f = lookup(controller);
    u32 screen, layout;
    if (f && f->screen->kind == RV_SCREEN_KIND_CANVAS &&
        rvx_prepare_canvas(&screen, &layout))
        core_clone_set_style(layout, core_template(CORE_TEMPLATE_CANVAS), f->style);
    return stock[ScreenController_activate_method / 4](controller, entering);
}

/* A pop deactivates the controller before the transition draws the screens
 * once more; the controller is destroyed after it. */
static PATCH_ARM u32 deactivate(void *controller, u32 leaving)
{
    typedef u32 (*Deactivate)(void *, u32);
    const Deactivate *stock = (const Deactivate *)OSOS_CONTROLLER_SCREEN_VTABLE;
    u32 result = stock[ScreenController_deactivate_method / 4](controller, leaving);
    struct frame *f = leaving ? lookup(controller) : NULL;
    if (f) {
        f->leaving = 1;
        /* Every canvas screen is the same layout, so a pop back to one
         * switches no layout: it takes that screen's style again and its view
         * is redrawn here. */
        struct frame *top = rvx_shown();
        if (top && top->screen->kind == RV_SCREEN_KIND_CANVAS) {
            u32 screen, layout;
            if (rvx_prepare_canvas(&screen, &layout))
                core_clone_set_style(layout, core_template(CORE_TEMPLATE_CANVAS),
                                     top->style);
            osos_ui_invalidate();
        }
    }
    return result;
}

static PATCH_ARM void *create_controller(const char *name, u32 screen, u32 layout,
                                         u32 transition)
{
    void *c = osos_controller_create_screen(name, screen, layout, transition);
    if (c)
        *(uintptr_t **)c = controller_table + 3;
    return c;
}

static __attribute__((noinline, noclone)) void
copy_methods(uintptr_t *dest, const uintptr_t *src, unsigned count)
{
    while (count--)
        *dest++ = *src++;
}

/* The system UI consumes the hold switch before any screen controller sees
 * it; the top canvas screen receives it as input kind 3 afterwards. */
static u32 hold_changed(u32 system, u32 locked, const void *next)
{
    u32 result = rvx_next(next, system, locked, 0, 0, 0, 0);
    struct frame *f = rvx_shown();
    if (f && f->screen->kind == RV_SCREEN_KIND_CANVAS && f->screen->canvas.input) {
        struct input in = {RV_INPUT_HOLD, 0, !!locked};
        f->screen->canvas.input(&f->ctx, &in);
    }
    return result;
}

static const struct rvx_hook hold_hook = {
    0x5fa8f8b9 /* SystemUI_HandleHoldChange_call */, (u32)hold_changed, 0, 0, 0};

void rvx_register_controller(void)
{
    copy_methods(view_table, (const uintptr_t *)(OSOS_VIEW_VTABLE - 12),
                 sizeof view_table / sizeof *view_table);
    copy_methods(controller_table,
                 (const uintptr_t *)(OSOS_CONTROLLER_SCREEN_VTABLE - 12),
                 sizeof controller_table / sizeof *controller_table);
    view_table[3 + View_draw_method / 4] = (uintptr_t)draw;
    controller_table[3 + ScreenController_event_method / 4] = (uintptr_t)event;
    controller_table[4] = (uintptr_t)destroy;
    controller_table[3 + ScreenController_deactivate_method / 4] =
        (uintptr_t)deactivate;
    controller_table[3 + ScreenController_activate_method / 4] =
        (uintptr_t)canvas_activate;
    osos_register_view_factory(CORE_VIEW_CANVAS, create_view);
    osos_ui_register_controllers(osos_controller_registry(), CORE_CANVAS_FACTORY, NULL,
                                 create_controller);
    rvx_bind_core(&hold_hook);
}
