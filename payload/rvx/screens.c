/* SPDX-License-Identifier: GPL-3.0-only */
#include "screen.h"
#include "screen_ui.h"
#include "cfw.h"
#include "osos.h"
#include "patch.h"

struct frame *rvx_top;
const struct context *rvx_origin;
static u32 wheel[13];
static int wheel_ready;

static void title(const char *s)
{
    unsigned n = 0;
    if (!s)
        s = "";
    while (s[n])
        n++;
    osos_resource_set(rvx_bank, 0x53747220, RVX_Title, s, n + 1);
}

static PATCH_ARM u32 destroy(void *controller, u32 mode);

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
    void *c;
    if (f->screen->kind == 7) {
        if (f->screen->stock.screen != 1) {
            osos_import_free(f);
            return NOT_FOUND;
        }
        f->stock_methods = osos_import_alloc(12 + 0x200);
        if (!f->stock_methods) {
            osos_import_free(f);
            return NO_MEMORY;
        }
        c = osos_create_screen_controller(osos_controller_registry(), 0x0dad065c,
                                          0x0dad065d, 3);
        if (c) {
            const uintptr_t *native = *(uintptr_t **)c;
            for (unsigned i = 0; i < (12 + 0x200) / 4; i++)
                f->stock_methods[i] = (native - 3)[i];
            f->stock_destroy = native[1];
            f->stock_methods[4] = (uintptr_t)destroy;
            *(uintptr_t **)c = f->stock_methods + 3;
        } else
            osos_import_free(f->stock_methods);
    } else {
        title(f->screen->title);
        c = osos_create_screen_controller(osos_controller_registry(), RVX_Screen,
                                          RVX_Layout, f->screen->transition ? 2 : 3);
    }
    if (!c) {
        osos_import_free(f);
        return NO_MEMORY;
    }
    f->ctx.controller = c;
    f->parent = rvx_top;
    f->split = f->screen->layout == 2 ||
               (!f->screen->layout && (rvx_top ? rvx_top->split : 1));
    if (f->screen->kind == 1 || f->screen->kind == 2)
        f->selected = rvx_get_int(f->ctx.self, f->screen->choice.key);
    rvx_top = f;
    if (f->screen->kind == 5 && f->screen->dynamic.initial)
        f->selected = f->screen->dynamic.initial(&f->ctx);
    osos_controller_push(parent, c);
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
            f->selected = row;
            osos_ui_invalidate();
            break;
        }
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
            f->selected = n < 0 ? 0 : (u32)n;
            present(f, ctx->controller);
        }
        osos_ui_invalidate();
        break;
    }
    }
}

static u32 count(struct frame *f)
{
    const struct screen *s = f->screen;
    switch (s->kind) {
    case 0:
        return s->list.count;
    case 1:
    case 2:
    case 6:
        return s->choice.count;
    case 3:
        return s->text.count;
    case 5:
        return s->dynamic.count ? s->dynamic.count(&f->ctx) : 0;
    default:
        return 0;
    }
}

static const char *setting_label(Pkg *p, uint16_t key)
{
    int n = rvx_get_int(p, key);
    const struct rvx_setting *s = rvx_declaration(p, key);
    if (s && s->labels && n >= 0 && (u32)n < s->label_count)
        return s->labels[n];
    return s && s->type == 1 ? (n ? "On" : "Off") : NULL;
}

void rvx_draw_page(struct frame *f, int width, int height)
{
    const struct graphics *g = &rvx_graphics;
    const struct screen *s = f->screen;
    g->fill(&(struct rect){0, 0, width, height}, g->color(0));
    if (f->context_count) {
        unsigned visible = height / 25;
        unsigned first =
            f->context_selection >= visible ? f->context_selection - visible + 1 : 0;
        for (unsigned j = first; j <= f->context_count && j - first < visible; j++)
            g->row((j - first) * 25, width,
                   j == f->context_count
                       ? "Cancel"
                       : s->context_rows[f->context_index[j]].row.label,
                   NULL, j == f->context_selection);
        return;
    }
    if (s->kind == 4) {
        if (s->canvas.draw)
            s->canvas.draw(&f->ctx, g, width, height);
        return;
    }
    u32 n = count(f);
    if (f->selected >= n)
        f->selected = n ? n - 1 : 0;
    if (s->kind == 2 && n) {
        struct rect track = {18, height / 2, width - 36, 12};
        g->fill(&track, g->color(4));
        if (n > 1)
            track.w = (u32)track.w * f->selected / (n - 1);
        g->bar(&track);
        g->text_in(&(struct rect){0, height / 2 - 35, width, 30}, 1, g->color(1),
                   s->choice.labels[f->selected], 0);
        return;
    }
    int row_height = s->kind == 5 && s->dynamic.detail ? 42 : 25;
    unsigned visible = height / row_height;
    u32 first = f->selected >= visible ? f->selected - visible + 1 : 0;
    if (s->kind == 3) {
        int y = 6;
        for (u32 j = f->selected; j < n && y < height; j++)
            y = g->paragraph(8, y, width - 16, 1, g->color(5), s->text.lines[j], 64) +
                6;
        return;
    }
    if (f->split && (s->kind == 0 || s->kind == 1 || s->kind == 5 || s->kind == 6)) {
        int pane_x = width / 2, pane_w = width - pane_x;
        const char *caption = s->caption;
        const struct bitmap *image = s->bitmap;
        if (s->kind == 0 && n && s->list.panes) {
            const struct pane *pane = s->list.panes + f->selected;
            caption = pane->text ? pane->text(&f->ctx) : pane->label;
            image = pane->image ? pane->image(&f->ctx) : pane->bitmap;
        } else if (s->kind == 1 && n)
            caption = s->choice.labels[f->selected];
        g->fill(&(struct rect){pane_x, 0, pane_w, height},
                g->mix(g->color(0), g->color(4), 20));
        g->paragraph(pane_x + 8, 8, pane_w - 16, 0, g->color(1), s->title, 2);
        if (image)
            g->bitmap(pane_x + (pane_w - 78) / 2, (height - 121) / 2, image);
        if (caption)
            g->paragraph(pane_x + 8, height - 48, pane_w - 16, 1, g->color(4), caption,
                         3);
        width = pane_x;
    }
    if (!n && s->kind == 5 && s->dynamic.empty)
        g->text_in(&(struct rect){0, 0, width, height}, 1, g->color(4),
                   s->dynamic.empty, 0);
    for (u32 j = first; j < n && j - first < visible; j++) {
        const char *label = "", *detail = NULL;
        if (s->kind == 0) {
            const struct row *r = s->list.rows + j;
            label = r->text ? r->text(&f->ctx) : r->label;
            if (r->kind == 2)
                detail = setting_label(f->ctx.self, r->key);
        } else if (s->kind == 5)
            label = s->dynamic.row(&f->ctx, j, &detail);
        else {
            label = s->choice.labels[j];
            int v = rvx_get_int(f->ctx.self, s->choice.key);
            if (s->kind == 6 ? (j < 32 && ((u32)v & (1u << j))) : (u32)v == j)
                detail = "✓";
        }
        int y = (j - first) * row_height;
        int selected = j == f->selected;
        int playing =
            s->kind == 5 && s->dynamic.status && s->dynamic.status(&f->ctx, j) == 1;
        if (row_height > 25) {
            if (selected)
                g->bar(&(struct rect){0, y, width, row_height});
            g->text(9, y + 1, 1, g->color(selected ? 3 : 1), label);
            g->text(9, y + 22, 2, g->color(selected ? 3 : 4), detail);
        } else
            g->row(y, width, label, detail, selected);
        if (playing || (s->kind == 5 && s->dynamic.detail == 2))
            g->text(width - 15, y + 4, 1, g->color(selected ? 3 : 1),
                    playing ? "▶" : "›");
    }
}

int rvx_input(struct frame *f, const struct input *in)
{
    const struct screen *s = f->screen;
    if (f->context_count) {
        if (in->kind == 0) {
            int n = f->context_selection + in->delta;
            f->context_selection = n < 0                  ? 0
                                   : n > f->context_count ? f->context_count
                                                          : n;
        } else if (in->kind == 2 && (in->button == 0 || in->button == 1)) {
            const struct row *r =
                in->button == 0 && f->context_selection < f->context_count
                    ? &s->context_rows[f->context_index[f->context_selection]].row
                    : NULL;
            f->context_count = 0;
            if (r) {
                struct context ctx = f->ctx;
                ctx.row = f->selected;
                rvx_act(&ctx, r);
            }
        }
        osos_ui_invalidate();
        return 1;
    }
    if (in->kind == 4 && in->button == 0 && s->context_count && count(f)) {
        struct context ctx = f->ctx;
        ctx.row = f->selected;
        for (unsigned i = 0; i < s->context_count && i < 16; i++)
            if (!s->context_rows[i].available || s->context_rows[i].available(&ctx))
                f->context_index[f->context_count++] = i;
        f->context_selection = 0;
        if (f->context_count) {
            osos_ui_invalidate();
            return 1;
        }
    }
    if (s->kind == 4 && s->canvas.input && s->canvas.input(&f->ctx, in))
        return 1;
    if (in->kind == 2 && in->button == 1) {
        rvx_pop(f->ctx.self);
        return 1;
    }
    u32 n = count(f);
    if (!n)
        return 0;
    if (in->kind == 0 ||
        (s->kind == 2 && in->kind == 2 && (in->button == 2 || in->button == 3))) {
        int delta = in->kind == 0 ? in->delta : (in->button == 2 ? -1 : 1);
        int64_t next = (int64_t)f->selected + delta;
        f->selected = next < 0 ? 0 : next >= n ? n - 1 : (u32)next;
        if (s->kind == 2)
            rvx_set_int(f->ctx.self, s->choice.key, f->selected);
    } else if (in->kind == 2 && in->button == 0 && f->selected < n) {
        if (s->kind == 0)
            rvx_act(&f->ctx, s->list.rows + f->selected);
        if (s->kind == 1 || s->kind == 2) {
            rvx_set_int(f->ctx.self, s->choice.key, f->selected);
            rvx_pop(f->ctx.self);
        } else if (s->kind == 6 && f->selected < 32) {
            rvx_set_int(f->ctx.self, s->choice.key,
                        rvx_get_int(f->ctx.self, s->choice.key) ^ (1u << f->selected));
        } else if (s->kind == 5 && s->dynamic.select &&
                   s->dynamic.select(&f->ctx, f->selected))
            rvx_pop(f->ctx.self);
    } else
        return 0;
    osos_ui_invalidate();
    return 1;
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
                if (in.kind == 4 && used && f->context_count)
                    f->held_button = k + 1;
                if (in.kind == 1)
                    f->consumed = used;
                if (used || (in.kind == 2 && f->consumed))
                    return 1;
            }
        }
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
    } else if (rvx_top) {
        struct frame *f = rvx_top;
        f->view = view;
        rvx_begin_draw(context, bounds, f->damage.w ? &f->damage : NULL);
        f->damage.w = 0;
        rvx_draw_page(f, width, height);
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
        orig = (Destroy)f->stock_destroy;
    if (f && f->screen->closed)
        f->screen->closed(&f->ctx);
    u32 result = orig(controller, mode);
    if (f) {
        struct frame **link = &rvx_top;
        while (*link && *link != f)
            link = &(*link)->parent;
        if (*link)
            *link = f->parent;
        if (rvx_top)
            title(rvx_top->screen->title);
        if (f->stock_methods)
            osos_import_free(f->stock_methods);
        Pkg *p = f->ctx.self;
        unsigned module = f->module;
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
    if (!wheel_ready) {
        osos_wheel_accumulator_init(wheel, 5, 1, 350);
        wheel_ready = 1;
    }
    osos_wheel_accumulator_reset(wheel);
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
    controller_table[3 + 0x30 / 4] = (uintptr_t)event;
    controller_table[4] = (uintptr_t)destroy;
    osos_register_view_factory(0x72767870, create_view);
    osos_register_view_factory(0x72767863, create_view);
    osos_ui_register_controllers(osos_controller_registry(), "RVXScreen", NULL,
                                 create_controller);
}

void cfw_rvx_hold(int locked)
{
    if (rvx_top && rvx_top->screen->kind == 4) {
        struct input in = {3, 0, !!locked};
        rvx_input(rvx_top, &in);
    }
}
