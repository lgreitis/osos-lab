/* SPDX-License-Identifier: GPL-3.0-only */
#include "native_ui.h"
#include "screen_ui.h"
#include "cfw.h"
#include "osos.h"
#include "patch.h"

struct frame *rvx_top;
const struct context *rvx_origin;
static u32 wheel[13];
static int wheel_ready;

static void refresh_title(void)
{
    void *model = ((void *(*)(void))0x081e0ff4)();
    if (model)
        osos_model_notify(model, OSOS_NOTIFY_ALL, RVX_Title);
}

static PATCH_ARM u32 destroy(void *controller, u32 mode);
static PATCH_ARM int event(void *controller, void *e);

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
    f->split = core_lists_kind(f->screen->kind) &&
               (f->screen->layout == RV_LAYOUT_SPLIT ||
                (f->screen->layout == RV_LAYOUT_AUTO && from == LayoutKind_SplitList));
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
    if (!wheel_ready) {
        osos_wheel_accumulator_init(wheel, 5, 1, 350);
        wheel_ready = 1;
    }
    osos_wheel_accumulator_reset(wheel);
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
            if (f->screen->kind == RV_SCREEN_KIND_SLIDER) {
                int pane = f->screen->transition == RV_TRANSITION_FROM_PANE &&
                           from == LayoutKind_SplitList;
                core_clone_set_style(
                    layout, core_template(CORE_TEMPLATE_SLIDER),
                    pane ? core_templates_enter_style(RES_Stopwatch_Template,
                                                      LayoutKind_SplitList)
                         : -1);
                if (from != LayoutKind_FullScreen)
                    transition = Transition_Standard;
            }
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
                if (dialog)
                    rvx_dialog_attach(f);
                *(uintptr_t **)c = f->stock_methods + 3;
            } else {
                rvx_top = f->parent;
                osos_import_free(f->stock_methods);
            }
        }
    } else {
        rvx_top = f;
        c = osos_create_screen_controller(osos_controller_registry(), RVX_Screen,
                                          RVX_Layout, f->screen->transition ? 2 : 3);
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
    if (rvx_top && rvx_top->ctx.self == p)
        ((int (*)(void *, u32))0x081034f0)(rvx_top->ctx.controller, 0);
}

void rvx_select(Pkg *p, const char *id, u32 row)
{
    for (struct frame *f = rvx_top; f; f = f->parent)
        if (f->ctx.self == p && id && cfw_string_equal(f->screen->id, id)) {
            /* A list's selection is a position: guests may sit before row. */
            u32 rows = rvx_row_count(f);
            if (f->screen->kind == RV_SCREEN_KIND_LIST && f->native) {
                row = rvx_guest_position(f->native, f->screen, row);
                rows += rvx_guest_count(f->native);
            }
            f->selected = row;
            if (f == rvx_top && f->native && row < rows)
                osos_ui_list_select(row);
            osos_ui_invalidate();
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

void rvx_act(const struct context *ctx, const struct row *r)
{
    Pkg *p = ctx->self;
    switch (r->kind) {
    case 0:
        open_id(p, r->screen, NULL, ctx->controller);
        break;
    case 1:
        if (r->action)
            r->action(ctx);
        break;
    case 2: {
        const struct rvx_setting *s = rvx_declaration(p, r->key);
        if (!s)
            break;
        int n = rvx_get_int(p, r->key);
        if (s->type == 1)
            rvx_set_int(p, r->key, !n);
        else if (!s->screen)
            rvx_set_int(p, r->key, n >= s->maximum ? s->minimum : n + 1);
        else {
            struct frame *f = frame(p, NULL);
            if (!f)
                break;
            f->screen = &f->setting_screen;
            f->setting_screen.title = s->label;
            f->setting_screen.kind = s->screen;
            f->setting_screen.choice.key = r->key;
            f->setting_screen.choice.labels = s->labels;
            f->setting_screen.choice.count = s->label_count;
            present(f, ctx->controller);
        }
        osos_ui_invalidate();
        break;
    }
    }
}

const char *rvx_setting_label(Pkg *p, uint16_t key)
{
    int n = rvx_get_int(p, key);
    const struct rvx_setting *s = rvx_declaration(p, key);
    if (s && s->screen == RV_SCREEN_KIND_MULTI_CHOICE)
        return NULL;
    if (s && s->labels && n >= 0 && (u32)n < s->label_count)
        return s->labels[n];
    return s && s->type == 1 ? (n ? "On" : "Off") : NULL;
}

static int rvx_input(struct frame *f, const struct input *in)
{
    const struct screen *s = f->screen;
    if (s->kind == RV_SCREEN_KIND_SLIDER) {
        int step = in->kind == 0 ? in->delta : 0;
        if (in->kind == 2 && (in->button == 2 || in->button == 3))
            step = in->button == 2 ? -1 : 1;
        if (step && s->choice.count) {
            int value = rvx_get_int(f->ctx.self, s->choice.key) + step;
            if (value < 0)
                value = 0;
            if ((u32)value >= s->choice.count)
                value = s->choice.count - 1;
            rvx_set_int(f->ctx.self, s->choice.key, value);
            return 1;
        }
        return 0;
    }
    if (s->kind == RV_SCREEN_KIND_CANVAS && s->canvas.input &&
        s->canvas.input(&f->ctx, in))
        return 1;
    if (in->kind == 2 && in->button == 1) {
        rvx_pop(f->ctx.self);
        return 1;
    }
    return 0;
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
    if (f) {
        const u32 *w = e;
        struct input in = {0};
        if (w[1] == 0x15) {
            osos_wheel_decode(e);
            in.delta = osos_wheel_accumulator_step(wheel, (int32_t)w[3], 0);
            if (!in.delta || rvx_input(f, &in))
                return 1;
        } else if (w[1] == 6 || w[1] == 0xd || w[1] == 8) {
            in.kind = w[1] == 6 ? 1 : w[1] == 8 ? 4 : 2;
            static const uint8_t keys[] = {0x6f, 0x6e, 0x59, 0x5a, 0x10};
            unsigned k = 0;
            while (k < 5 && keys[k] != osos_button_ui_key(e))
                k++;
            if (k < 5) {
                if (in.kind == 2 && f->held_button == (int)k + 1) {
                    f->held_button = 0;
                    return 1;
                }
                in.button = k;
                int used = rvx_input(f, &in);
                if (lookup(controller) != f)
                    return 1;
                if (in.kind == 4 && used)
                    f->held_button = k + 1;
                if (in.kind == 1)
                    f->consumed = used;
                if (used || (in.kind == 2 && f->consumed))
                    return 1;
            }
        }
    }
    if (f && f->native_methods) {
        typedef int (*Event)(void *, void *);
        return ((Event)f->native_methods[ScreenController_event_method / 4])(controller,
                                                                             e);
    }
    return osos_controller_handle_event(controller, e);
}

static PATCH_ARM void draw(void *view, void *arg, const int32_t *clip)
{
    (void)arg;
    (void)clip;
    u32 context[17];
    int32_t bounds[4];
    osos_graphics_construct(context);
    osos_view_bind_graphics(view, context);
    osos_view_local_rect(view, bounds);
    int width = bounds[3] - bounds[1], height = bounds[2] - bounds[0];
    if (osos_view_id(view) == RVX_PaneView) {
        rvx_begin_draw(context, bounds, NULL);
        rvx_draw_menu_pane(width, height);
    } else if (rvx_top && rvx_top->screen->kind == RV_SCREEN_KIND_CANVAS) {
        struct frame *f = rvx_top;
        f->view = view;
        rvx_begin_draw(context, bounds, f->damage.w ? &f->damage : NULL);
        f->damage.w = 0;
        rvx_graphics.fill(&(struct rect){0, 0, width, height}, rvx_graphics.color(0));
        if (f->screen->canvas.draw)
            f->screen->canvas.draw(&f->ctx, &rvx_graphics, width, height);
    }
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

void rvx_register_controller(void)
{
    copy_methods(view_table, (const uintptr_t *)(OSOS_VIEW_VTABLE - 12),
                 sizeof view_table / sizeof *view_table);
    copy_methods(controller_table,
                 (const uintptr_t *)(OSOS_CONTROLLER_SCREEN_VTABLE - 12),
                 sizeof controller_table / sizeof *controller_table);
    view_table[3 + 0x74 / 4] = (uintptr_t)draw;
    controller_table[3 + ScreenController_event_method / 4] = (uintptr_t)event;
    controller_table[4] = (uintptr_t)destroy;
    osos_register_view_factory(0x72767870, create_view);
    osos_register_view_factory(0x72767863, create_view);
    osos_ui_register_controllers(osos_controller_registry(), "RVXScreen", NULL,
                                 create_controller);
}

void cfw_rvx_hold(int locked)
{
    if (rvx_top && rvx_top->screen->kind == RV_SCREEN_KIND_CANVAS) {
        struct input in = {3, 0, !!locked};
        rvx_input(rvx_top, &in);
    }
}
