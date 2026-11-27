/* SPDX-License-Identifier: GPL-3.0-only */
#include "screen.h"
#include "native_ui.h"
#include "cfw.h"
#include "osos.h"
#include "patch.h"

struct rvx_block {
    struct rvx_block *next;
    u32 bytes;
};

void *rvx_alloc(Pkg *p, u32 bytes)
{
    if (bytes > 0x7ffffff0)
        return NULL;
    struct rvx_block *b = osos_import_alloc(sizeof *b + bytes);
    if (!b)
        return NULL;
    b->bytes = bytes;
    b->next = p->heap;
    p->heap = b;
    return b + 1;
}

void rvx_free(Pkg *p, void *data)
{
    for (struct rvx_block **it = &p->heap; *it; it = &(*it)->next) {
        struct rvx_block *b = *it;
        if (b + 1 == data) {
            *it = b->next;
            rvx_forget_colors(p, data, b->bytes);
            rvx_forget_provider(p, data, b->bytes);
            osos_import_free(b);
            return;
        }
    }
}

struct rvx_timer {
    const void *vtable;
    u32 native[11];
    struct rvx_timer *next;
    Pkg *pkg;
    void (*callback)(void *);
    void *arg;
    int repeat, firing, cancelled;
};

typedef void (*TimerOp)(void *);

static void stop(Pkg *p, struct rvx_timer *t)
{
    if (!t)
        return;
    struct rvx_timer **it = &p->timers;
    while (*it && *it != t)
        it = &(*it)->next;
    if (!*it)
        return;
    ((TimerOp)0x081219d0)(t->native);
    if (t->firing) {
        t->cancelled = 1;
        return;
    }
    *it = t->next;
    osos_import_free(t);
}

static PATCH_ARM u32 event(struct rvx_timer *t, const void *event)
{
    if (!event || ((const u32 *)event)[1] != 15)
        return 0;
    t->firing = 1;
    t->callback(t->arg);
    t->firing = 0;
    if (!t->repeat || t->cancelled)
        stop(t->pkg, t);
    else
        ((TimerOp)0x08121268)(t->native);
    return 1;
}

static const void *const timer_vtable[13] = {[12] = event};

static struct rvx_timer *timer_start(Pkg *p, u32 ms, int repeat, void (*cb)(void *),
                                     void *arg)
{
    if (!ms || !cb)
        return NULL;
    struct rvx_timer *t = osos_import_alloc(sizeof *t);
    if (!t)
        return NULL;
    t->vtable = timer_vtable;
    t->pkg = p;
    t->callback = cb;
    t->arg = arg;
    t->repeat = repeat;
    t->next = p->timers;
    p->timers = t;
    typedef void (*Init)(void *, void *, u32, void *);
    ((Init)0x0810635c)(t, t->native, ms, NULL);
    ((void (*)(void *, u32))0x0812195c)(t->native, ms);
    ((TimerOp)0x08121268)(t->native);
    return t;
}

void rvx_stop_timers(Pkg *p, const struct rvx_module *m)
{
    for (struct rvx_timer *t = p->timers, *next; t; t = next) {
        next = t->next;
        if (!m || (uintptr_t)t->callback - (uintptr_t)m->image < m->text)
            stop(p, t);
    }
}

void rvx_cleanup(Pkg *p)
{
    rvx_stop_timers(p, NULL);
    while (p->files)
        rvx_file_close(p, p->files);
    while (p->heap)
        rvx_free(p, p->heap + 1);
}

static u32 format(char *out, u32 cap, u32 n, const char *suffix)
{
    char digits[16];
    u32 i = 0, j = 0, group = 0;
    do {
        if (group == 3) {
            digits[i++] = ',';
            group = 0;
        }
        digits[i++] = '0' + n % 10;
        n /= 10;
        group++;
    } while (n);
    while (i && j + 1 < cap)
        out[j++] = digits[--i];
    while (*suffix && j + 1 < cap)
        out[j++] = *suffix++;
    if (cap)
        out[j] = 0;
    return j;
}

static u32 number(char *out, u32 cap, u32 n)
{
    return format(out, cap, n, "");
}

static u32 size_text(char *out, u32 cap, u32 n)
{
    u32 unit = n >= 1048576 ? 1048576 : 1024;
    u32 tenth = ((n % unit) * 10 + unit / 2) / unit;
    char suffix[] = {'.', '0' + tenth % 10, ' ', unit == 1024 ? 'K' : 'M', 'B', 0};
    return format(out, cap, n / unit + tenth / 10, suffix);
}

static const void *no_debug(void)
{
    return NULL;
}

/* Kept in guest memory for emulator inspection, including early boot. */
static volatile char rvx_last_log[160];

static void log_text(Pkg *p, const char *s)
{
    unsigned n = 0;
    while (p->id[n]) {
        rvx_last_log[n] = p->id[n];
        n++;
    }
    rvx_last_log[n++] = ' ';
    while (s && *s && n + 1 < sizeof rvx_last_log)
        rvx_last_log[n++] = *s++;
    rvx_last_log[n] = 0;
}

#define VIEW_TREE_DEPTH 16 /* deeper than any stock screen; bounds a broken tree */

/* A view on its own compositor layer keeps its pixels until its own window is
 * dirtied, so a change to colours or artwork dirties every view. Only container
 * views (their find slot is View_FindViewById) carry the child array. */
static void invalidate_tree(uint8_t *view, uint32_t depth)
{
    NATIVE_CALL(View_InvalidateRect)(view, (const int32_t *)(view + View_bounds));
    void **vtable = RV_FIELD(view, 0, void **);
    if (depth > VIEW_TREE_DEPTH ||
        vtable[View_find_view_method / 4] != (void *)ADDR_View_FindViewById)
        return;
    uint32_t count = RV_FIELD(view, View_child_count, uint32_t);
    uint8_t **children = RV_FIELD(view, View_child_items, uint8_t **);
    for (uint32_t i = 0; children && i < count; i++)
        if (children[i])
            invalidate_tree(children[i], depth + 1);
}

static void invalidate(Pkg *p)
{
    if (rvx_top && rvx_top->ctx.self == p)
        rvx_top->damage.w = 0;
    osos_ui_invalidate();
}

static void redraw(Pkg *p)
{
    invalidate(p);
    uint8_t *root = NATIVE_CALL(UI_GetRootView)();
    if (root)
        invalidate_tree(root, 0);
}

static void refresh(Pkg *p, const char *s)
{
    for (struct frame *f = rvx_top; f; f = f->parent)
        if (f->ctx.self == p &&
            (!s || (f->screen->id && cfw_string_equal(f->screen->id, s))))
            rvx_native_refresh(f);
    invalidate(p);
}

static void invalidate_rect(Pkg *p, const void *rect)
{
    struct frame *f = rvx_top;
    const struct rect *r = rect;
    if (!f || f->ctx.self != p || !r || !f->view || r->w <= 0 || r->h <= 0)
        return;
    int x = r->x, y = r->y, right = x + r->w, bottom = y + r->h;
    if (f->damage.w) {
        if (x > f->damage.x)
            x = f->damage.x;
        if (y > f->damage.y)
            y = f->damage.y;
        if (right < f->damage.x + f->damage.w)
            right = f->damage.x + f->damage.w;
        if (bottom < f->damage.y + f->damage.h)
            bottom = f->damage.y + f->damage.h;
    }
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (right > 320)
        right = 320;
    if (bottom > 217)
        bottom = 217;
    if (x >= right || y >= bottom)
        return;
    f->damage = (struct rect){x, y, right - x, bottom - y};
    osos_ui_invalidate();
}

static u32 string_id(Pkg *p, const char *s)
{
    static u32 id = 0x72580000;
    u32 bytes = 0;
    while (s && s[bytes])
        bytes++;
    if (!s || bytes > 65534)
        return 0;
    if (!rvx_bank || p->string_count == 4096)
        return 0;
    if (!p->string_base) {
        p->string_base = id;
        id += 4096;
    }
    u32 assigned = p->string_base + p->string_count++;
    osos_resource_set(rvx_bank, 0x53747220, assigned, s, bytes + 1);
    return assigned;
}

const struct rvx_api cfw_rvx_api = {
    .size = sizeof(struct rvx_api),
    .version = RVX_API,
    .bind = rvx_bind,
    .unbind = rvx_unbind,
    .next = rvx_next,
    .get = rvx_get_int,
    .set = rvx_set_int,
    .read = rvx_read_setting,
    .write = rvx_write_setting,
    .flush = cfw_rvx_flush,
    .alloc = rvx_alloc,
    .free = rvx_free,
    .load = rvx_load,
    .unload = rvx_unload,
    .log = log_text,
    .push = rvx_push,
    .pop = rvx_pop,
    .refresh = refresh,
    .invalidate = invalidate,
    .debug = no_debug,
    .asset_load = rvx_asset_load,
    .asset_read = rvx_asset_read,
    .number = number,
    .size_text = size_text,
    .timer_start = timer_start,
    .timer_stop = stop,
    .rect = invalidate_rect,
    .pane = rvx_refresh_pane,
    .open = rvx_file_open,
    .file_read = rvx_file_read,
    .file_write = rvx_file_write,
    .seek = rvx_file_seek,
    .close = rvx_file_close,
    .remove = rvx_file_delete,
    .list = rvx_folder_list,
    .lock = cfw_rvx_lock,
    .unlock = cfw_rvx_unlock,
    .select = rvx_select,
    .string = string_id,
    .content_open = rvx_content_open,
    .content_list = rvx_content_list,
    .asset_open = rvx_asset_open,
    .colors = rvx_colors,
    .redraw = redraw,
    .next7 = rvx_next7,
    .service = rvx_service,
    .provider = rvx_provider,
    .firmware = rvx_firmware_open,
    .extension_service = rvx_extension_service,
};
