/* SPDX-License-Identifier: GPL-3.0-only */
#include "screen.h"
#include "native_ui.h"
#include "cfw.h"
#include "osos.h"
#include "patch.h"

/* A block an extension holds: linked both ways on its owner's list, so a free
 * unlinks it without a walk, 16 bytes so the data stays 8-byte aligned. */
struct rvx_block {
    struct rvx_block *next, **link;
    Pkg *owner;
    u32 bytes;
};

static struct rvx_block *core_heap; /* the blocks of the runtime's own screens */

void *rvx_alloc(Pkg *p, u32 bytes)
{
    struct rvx_block **heap = p ? &p->heap : &core_heap;
    if (bytes > 0x7ffffff0)
        return NULL;
    struct rvx_block *b = osos_import_alloc(sizeof *b + bytes);
    if (!b)
        return NULL;
    b->owner = p;
    b->bytes = bytes;
    /* Masked: an extension may allocate and free on the USB task. */
    u32 irq = cfw_rvx_lock();
    b->next = *heap;
    b->link = heap;
    if (b->next)
        b->next->link = &b->next;
    *heap = b;
    cfw_rvx_unlock(irq);
    return b + 1;
}

void rvx_free(Pkg *p, void *data)
{
    struct rvx_block *b = data ? (struct rvx_block *)data - 1 : NULL;
    if (!b || b->owner != p || !b->link)
        return;
    u32 irq = cfw_rvx_lock();
    *b->link = b->next;
    if (b->next)
        b->next->link = b->link;
    b->link = NULL;
    cfw_rvx_unlock(irq);
    if (p) {
        rvx_forget_colors(p, data, b->bytes);
        rvx_forget_provider(p, data, b->bytes);
    }
    osos_import_free(b);
}

/* Extension timers: each is its own recipient, a native timer and the
 * callback. A stopped timer stays allocated for two grace ticks, because an
 * event it posted before the stop may still be in the queue; the grace timer
 * runs only while a stopped one waits. */
struct rvx_timer {
    const void *vtable; /* the recipient's: only the event slot */
    u32 native[EventTimer_bytes / 4];
    struct rvx_timer *next;
    Pkg *pkg;
    void (*callback)(void *); /* NULL once stopped */
    void *arg;
    uint8_t repeat, retiring; /* grace ticks left before it is freed */
};

typedef void (*TimerOp)(void *);
typedef void (*TimerInit)(void *, void *, u32, void *);

enum { GRACE_MS = 1000 };
static struct {
    const void *vtable;
} grace;
static u32 grace_timer[EventTimer_bytes / 4];
static uint8_t grace_ready, grace_armed;

static void arm_grace(void)
{
    if (!grace_ready) {
        ((TimerInit)ADDR_UI_InitializeEventTimer)(&grace, grace_timer, GRACE_MS, NULL);
        ((void (*)(void *, u32))ADDR_Timer_SetPeriod)(grace_timer, GRACE_MS);
        grace_ready = 1;
    }
    if (!grace_armed) {
        ((TimerOp)ADDR_Timer_Start)(grace_timer);
        grace_armed = 1;
    }
}

static PATCH_ARM u32 grace_event(void *self, const uint8_t *event)
{
    (void)self;
    if (!event || RV_FIELD(event, Event_kind, u32) != Event_Timer)
        return 0;
    int waiting = 0;
    grace_armed = 0;
    for (Pkg *p = rvx_packages; p; p = p->next)
        for (struct rvx_timer **link = &p->timers; *link;) {
            struct rvx_timer *t = *link;
            if (t->retiring && --t->retiring == 0) {
                *link = t->next;
                osos_import_free(t);
                continue;
            }
            waiting |= t->retiring != 0;
            link = &t->next;
        }
    if (waiting)
        arm_grace();
    return 1;
}

static const void *const grace_vtable[EventRecipient_event_method / 4 + 1] = {
    [EventRecipient_event_method / 4] = grace_event};

static void stop(Pkg *p, struct rvx_timer *t)
{
    if (!t)
        return;
    struct rvx_timer *it = p->timers;
    while (it && it != t)
        it = it->next;
    if (!it || t->retiring)
        return;
    ((TimerOp)ADDR_Timer_Stop)(t->native);
    t->callback = NULL;
    t->retiring = 2;
    grace.vtable = grace_vtable;
    arm_grace();
}

/* A repeating timer is armed again before its callback runs, so the period
 * does not drift by the callback's time. */
static PATCH_ARM u32 event(struct rvx_timer *t, const uint8_t *event)
{
    if (!event || RV_FIELD(event, Event_kind, u32) != Event_Timer)
        return 0;
    void (*callback)(void *) = t->callback;
    if (callback && t->repeat)
        ((TimerOp)ADDR_Timer_Start)(t->native);
    else if (!t->retiring)
        stop(t->pkg, t);
    if (callback)
        callback(t->arg);
    return 1;
}

static const void *const timer_vtable[EventRecipient_event_method / 4 + 1] = {
    [EventRecipient_event_method / 4] = event};

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
    t->repeat = repeat != 0;
    t->retiring = 0;
    t->next = p->timers;
    p->timers = t;
    ((TimerInit)ADDR_UI_InitializeEventTimer)(t, t->native, ms, NULL);
    ((void (*)(void *, u32))ADDR_Timer_SetPeriod)(t->native, ms);
    ((TimerOp)ADDR_Timer_Start)(t->native);
    return t;
}

void rvx_stop_timers(Pkg *p, const struct rvx_module *m)
{
    for (struct rvx_timer *t = p->timers, *next; t; t = next) {
        next = t->next;
        if (t->callback &&
            (!m || (uintptr_t)t->callback - (uintptr_t)m->image < m->text))
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

/* The canvas view's rectangle {top, left, bottom, right} in the root's space,
 * or NULL when the caller's canvas is not on top. */
static const int32_t *canvas_view(Pkg *p)
{
    struct frame *f = rvx_shown();
    if (!f || f->ctx.self != p || f->screen->kind != RV_SCREEN_KIND_CANVAS || !f->view)
        return NULL;
    return (const int32_t *)((const uint8_t *)f->view + View_bounds);
}

static void invalidate(Pkg *p)
{
    const int32_t *view = canvas_view(p);
    if (view)
        osos_ui_invalidate_rect(view);
}

static void redraw(Pkg *p)
{
    (void)p;
    osos_ui_invalidate();
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
    const int32_t *view = canvas_view(p);
    const struct rect *r = rect;
    if (!view || !r || r->w <= 0 || r->h <= 0)
        return;
    /* Only that part of the canvas, in the root's space; the firmware clips
     * the draw to what it repaints. */
    int32_t part[] = {view[0] + r->y, view[1] + r->x, view[0] + r->y + r->h,
                      view[1] + r->x + r->w};
    osos_ui_invalidate_rect(part);
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
    .stock_view = rvx_stock_view,
};
