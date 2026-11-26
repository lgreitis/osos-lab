/* SPDX-License-Identifier: GPL-3.0-only */
#include "native_ui.h"
#include "cfw.h"
#include "patch.h"

static struct core_screen *layouts;

static void notify(u32 id)
{
    void *model = NATIVE_CALL(SettingsModel_GetInstance)();
    if (model)
        osos_model_notify(model, OSOS_NOTIFY_ALL, id);
}

const struct core_screen *rvx_native_table_screen(u32 table)
{
    for (const struct core_screen *s = layouts; s; s = s->next)
        if (s->id + 3 == table)
            return s;
    return NULL;
}

const struct core_screen *rvx_native_screen_of(u32 screen, int *split)
{
    for (const struct core_screen *s = layouts; s; s = s->next)
        for (*split = 0; core_lists_kind(s->kind) && *split < 2; (*split)++)
            if (core_lists_screen_id(s, *split) == screen)
                return s;
    return NULL;
}

int rvx_prepare_native(struct frame *f, u32 *screen_id, u32 *layout_id)
{
    const struct screen *d = f->screen;
    const char *name = d->id ? d->id : "";
    u32 key = d->id ? 0 : d->choice.key;
    struct core_screen *s;
    for (s = layouts; s; s = s->next)
        if (s->owner == f->ctx.self && s->kind == d->kind && s->key == key &&
            cfw_string_equal(name, s->name))
            break;
    if (!s) {
        unsigned length = 0;
        while (name[length])
            length++;
        s = osos_import_alloc(sizeof(*s) + length + 1);
        if (!s)
            return 0;
        for (unsigned i = 0; i <= length; i++)
            s->name[i] = name[i];
        s->owner = f->ctx.self;
        s->key = key;
        s->kind = d->kind;
        s->ids = core_clone_ids(d);
        s->id = core_resource_take(s->ids);
        if (!s->id) {
            osos_import_free(s);
            return 0;
        }
        s->next = layouts;
        layouts = s;
    }
    int list = core_lists_kind(d->kind);
    int split = list && f->split;
    unsigned variant = 1u << split;
    if (!(s->built & variant)) {
        s->declaration = d;
        int ok = list                               ? core_clone_list(s, split)
                 : d->kind == RV_SCREEN_KIND_TEXT   ? core_clone_text(s)
                 : d->kind == RV_SCREEN_KIND_DIALOG ? core_clone_dialog(s)
                                                    : core_clone_slider(s);
        s->declaration = NULL;
        if (!ok)
            return 0;
        s->built |= variant;
    }
    f->native = s;
    *screen_id = split ? core_lists_screen_id(s, 1) : s->id;
    *layout_id = *screen_id + 1;
    return 1;
}

static struct frame *owner_of(u32 id)
{
    if ((id >> 24) != 0x71)
        return NULL;
    for (struct frame *f = rvx_top; f; f = f->parent)
        if (f->native && id - f->native->id < f->native->ids)
            return f;
    return NULL;
}

u32 rvx_row_count(struct frame *f)
{
    const struct screen *d = f->screen;
    switch (d->kind) {
    case RV_SCREEN_KIND_LIST:
        return d->list.count;
    case RV_SCREEN_KIND_DYNAMIC_LIST:
        return d->dynamic.count ? d->dynamic.count(&f->ctx) : 0;
    case RV_SCREEN_KIND_MULTI_CHOICE:
        return d->choice.count > 32 ? 32 : d->choice.count;
    case RV_SCREEN_KIND_CHOICE:
        return d->choice.count;
    default:
        return 0;
    }
}

static const char *row_label(const struct row *row, struct frame *f)
{
    return row->text ? row->text(&f->ctx) : row->label;
}

/* A dialog's texts from its message source on (native_layout.c): the message,
 * the detail, then past the buttons' source one label per button. */
static const char *dialog_text(const struct screen *d, u32 index)
{
    if (index == 0)
        return d->dialog.message;
    if (index == 1)
        return d->dialog.detail;
    return index >= 3 && index - 3 < d->dialog.count ? d->dialog.buttons[index - 3]
                                                     : NULL;
}

int rvx_native_property(u32 type, u32 id, uintptr_t *out)
{
    struct frame *f = owner_of(id);
    if (!f)
        return 0;
    const struct screen *d = f->screen;
    const struct core_screen *s = f->native;
    u32 rows = CORE_SCREEN_ROWS(s), index = (id - rows) / 2;
    const char *text = "";
    if (id == s->id + 2 && type == Property_String)
        text = d->title;
    else if (d->kind == RV_SCREEN_KIND_TEXT && type == Property_String &&
             id - rows < d->text.count)
        text = d->text.lines[id - rows];
    else if (d->kind == RV_SCREEN_KIND_DIALOG && type == Property_String &&
             id - rows < 3u + d->dialog.count)
        text = dialog_text(d, id - rows);
    else if (d->kind == RV_SCREEN_KIND_SLIDER && id - rows < 2) {
        u32 count = d->choice.count;
        u32 value = (u32)rvx_get_int(f->ctx.self, d->choice.key);
        if (value >= count)
            value = 0;
        if (id == rows && type == Property_Control) {
            f->range[0] = value;
            f->range[1] = 0;
            f->range[2] = count > 1 ? count - 1 : 1;
            *out = (uintptr_t)f->range;
            return 1;
        }
        if (id != rows + 1 || type != Property_String)
            return 0;
        text = count ? d->choice.labels[value] : "";
    } else if (d->kind == RV_SCREEN_KIND_LIST && index < d->list.count) {
        const struct row *row = d->list.rows + index;
        if (type == Property_Count) {
            *out = 1;
            return 1;
        }
        if (type != Property_String)
            return 0;
        text = (id - rows) % 2 ? (row->kind == RV_ROW_KIND_SETTING
                                      ? rvx_setting_label(f->ctx.self, row->key)
                                      : "")
                               : row_label(row, f);
    } else if (core_lists_indexed(d->kind) && id == rows) {
        static const u32 submenu[] = {MenuRowStyle_TwoLineSubmenu, 0};
        if (type == Property_Count)
            *out = rvx_row_count(f);
        else if (type == Property_LayoutSet && d->kind == RV_SCREEN_KIND_DYNAMIC_LIST &&
                 d->dynamic.detail == RV_DETAIL_BELOW_SUBMENU)
            *out = (uintptr_t)submenu;
        else
            return 0;
        return 1;
    } else if (core_lists_kind(d->kind) && f->split &&
               id - CORE_LIST_PANE_CAPTION(core_lists_screen_id(s, 1)) < 4) {
        u32 field = id - CORE_LIST_PANE_CAPTION(core_lists_screen_id(s, 1));
        /* A list's highlight is a position: a declared row's or a guest's. */
        u32 selected = f->selected;
        const struct installed *guest = d->kind == RV_SCREEN_KIND_LIST
                                            ? rvx_guest_at(s, d, f->selected, &selected)
                                            : NULL;
        if (guest)
            return rvx_guest_pane_property(guest, f->ctx.controller, field, type, out);
        const struct pane *p =
            d->kind == RV_SCREEN_KIND_LIST && d->list.panes && selected < d->list.count
                ? d->list.panes + selected
                : NULL;
        if ((field == 1 || field == 2) && type == Property_Bitmap) {
            const struct bitmap *b =
                p ? (p->image ? p->image(&f->ctx) : p->bitmap) : d->bitmap;
            u32 size = 0;
            const u32 *image = rvx_frame_bitmap(f, b, &size);
            *out = (uintptr_t)rvx_clear_bitmap;
            if (rvx_is_pane_icon(image, size))
                *out = field == 1
                           ? (uintptr_t)image
                           : osos_ui_bitmap(RES_SettingsPreview_Background_Image);
            return 1;
        }
        if (type != Property_String)
            return 0;
        if (field == 3)
            text = p ? row_label(d->list.rows + selected, f) : d->title;
        else if (field == 0 && p)
            text = p->text ? p->text(&f->ctx) : p->label;
        else if (field == 0 && d->kind == RV_SCREEN_KIND_CHOICE &&
                 f->selected < d->choice.count)
            text = d->choice.labels[f->selected];
        else if (field == 0)
            text = d->caption;
        else
            return 0;
    } else if (type == Property_String && d->kind == RV_SCREEN_KIND_DYNAMIC_LIST &&
               id == s->id + 3)
        text = rvx_row_count(f) ? "" : d->dynamic.empty;
    else
        return 0;
    *out = (uintptr_t)(text ? text : "");
    return 1;
}

int cfw_rvx_indexed_property(u32 type, u32 id, u32 index, uintptr_t *out)
{
    struct frame *f = owner_of(id);
    if (!f || id - CORE_SCREEN_ROWS(f->native) > 1 ||
        !core_lists_indexed(f->screen->kind) || index >= rvx_row_count(f))
        return 0;
    const struct screen *d = f->screen;
    int title = id == CORE_SCREEN_ROWS(f->native);
    if (type == Property_String) {
        const char *detail = NULL;
        const char *label = d->kind == RV_SCREEN_KIND_DYNAMIC_LIST
                                ? d->dynamic.row(&f->ctx, index, &detail)
                                : d->choice.labels[index];
        const char *value = title ? label : detail;
        *out = (uintptr_t)(value ? value : "");
    } else if (title && type == Property_Layout &&
               d->kind == RV_SCREEN_KIND_DYNAMIC_LIST &&
               d->dynamic.detail == RV_DETAIL_BELOW_SUBMENU)
        *out = MenuRowStyle_TwoLineSubmenu;
    else if (title && type == Property_Bitmap) {
        u32 bitmap = 0;
        if (d->kind == RV_SCREEN_KIND_DYNAMIC_LIST) {
            if (d->dynamic.status &&
                d->dynamic.status(&f->ctx, index) == RV_ROW_STATUS_PLAYING)
                bitmap = OSOS_MENU_PLAYING_BITMAP;
        } else {
            u32 value = rvx_get_int(f->ctx.self, d->choice.key);
            if (d->kind == RV_SCREEN_KIND_CHOICE ? value == index
                                                 : ((value >> index) & 1u))
                bitmap = OSOS_MENU_CHECKED_BITMAP;
        }
        *out = bitmap
                   ? osos_ui_bitmap(
                         bitmap + (index == osos_selection_index(osos_ui_selection())))
                   : 0;
    } else
        return 0;
    return 1;
}

static void refresh_pane(struct frame *f)
{
    if (!f->split || !core_lists_kind(f->screen->kind))
        return;
    u32 base = CORE_LIST_PANE_CAPTION(core_lists_screen_id(f->native, 1));
    for (unsigned i = 0; i < 4; i++)
        notify(base + i);
}

void rvx_native_refresh(struct frame *f)
{
    if (!f || !f->native)
        return;
    u32 first = CORE_SCREEN_ROWS(f->native);
    if (f->screen->kind == RV_SCREEN_KIND_LIST)
        for (u32 i = 0; i < 2 * f->screen->list.count; i++)
            notify(first + i);
    else {
        notify(first);
        notify(first + 1);
        if (f->screen->kind == RV_SCREEN_KIND_DYNAMIC_LIST)
            notify(f->native->id + 3);
    }
    refresh_pane(f);
}

static unsigned menu_count(const struct screen *d)
{
    return d->context_count < 16 ? d->context_count : 16;
}

static u32 menu_base(const struct frame *f)
{
    return f->native->id + f->native->ids - CORE_LIST_SPLIT_IDS -
           core_lists_context_ids(f->screen);
}

static void open_context(struct frame *f)
{
    const struct screen *d = f->screen;
    u32 count = menu_count(d), base = menu_base(f);
    struct context ctx = f->ctx;
    ctx.row = f->selected = osos_selection_index(osos_ui_selection());
    /* A list's rows are its declared rows and the guests among them. */
    u32 guests = d->kind == RV_SCREEN_KIND_LIST ? rvx_guest_count(f->native) : 0;
    if (f->selected >= rvx_row_count(f) + guests)
        return;
    if (!f->context_actions)
        f->context_actions = osos_import_alloc(count * sizeof *f->context_actions);
    if (!f->context_actions)
        return;
    u32 store[ContextMenuItems_bytes / 4];
    struct osos_context_menu *menu = (void *)store;
    osos_context_menu_init(menu);
    unsigned shown = 0;
    for (u32 i = 0; i < count; i++) {
        if (d->context_rows[i].available && !d->context_rows[i].available(&ctx))
            continue;
        core_screens_handler_name(f->context_actions[i], base + 1 + i);
        osos_context_menu_append(menu, base + 1 + i, f->context_actions[i], 1);
        shown++;
    }
    if (shown) {
        osos_context_menu_append(menu, RES_ContextMenu_Cancel_String, "HandleCancel",
                                 0);
        void *model = NATIVE_CALL(ContextMenuModel_Get)();
        NATIVE_CALL(ContextMenuModel_SetItems)(model, menu);
        u32 id = base + count + 1;
        NATIVE_CALL(UI_SetScreenState)(
            NATIVE_CALL(UI_GetLayoutManager)(), id, 0, 0,
            NATIVE_CALL(ContextMenuModel_GetRememberedIndex)(model), 0);
        void *c =
            osos_create_screen_controller(osos_controller_registry(), id, id + 1, 0);
        if (c)
            osos_controller_push(f->ctx.controller, c);
    }
    osos_context_menu_destroy(menu);
}

PATCH_ARM int rvx_native_action(void *controller, const char *name, u32 argument)
{
    struct frame *f = rvx_top;
    while (f && f->ctx.controller != controller)
        f = f->parent;
    if (!f)
        return osos_settings_action(controller, name, argument);
    u32 id = 0;
    if (name && name[0] == 'R' && name[1] == 'v') {
        unsigned i = 2;
        for (; i < 10; i++) {
            const char *p = name + i;
            unsigned digit = (unsigned char)*p - '0';
            if (digit > 9)
                digit = (unsigned char)*p - 'a' + 10;
            if (digit > 15 || (*p > '9' && *p < 'a'))
                break;
            id = (id << 4) | digit;
        }
        if (i != 10 || name[i])
            id = 0;
    }
    if (!id && rvx_menu_action(controller, name, argument, 0))
        return 1; /* a guest row of this list */
    if (!id || owner_of(id) != f) {
        typedef int (*Action)(void *, const char *, u32);
        return ((Action)f->native_methods[ScreenController_action_method / 4])(
            controller, name, argument);
    }
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    const struct screen *d = f->screen;
    u32 base = menu_base(f), rows = CORE_SCREEN_ROWS(f->native);
    if (d->context_count && id == base)
        open_context(f);
    else if (d->context_count && id - base - 1 < menu_count(d)) {
        struct context ctx = f->ctx;
        ctx.row = f->selected;
        ctx.list = controller;
        const struct context *prior = rvx_origin;
        rvx_origin = &ctx;
        rvx_act(&ctx, &d->context_rows[id - base - 1].row);
        rvx_origin = prior;
    } else if (f->split &&
               id == CORE_LIST_PANE_CAPTION(core_lists_screen_id(f->native, 1))) {
        f->selected = osos_selection_index(osos_ui_selection());
        refresh_pane(f);
    } else {
        u32 i = d->kind == RV_SCREEN_KIND_LIST
                    ? (id - rows) / 2
                    : osos_selection_index(osos_ui_selection());
        if (i >= rvx_row_count(f))
            return 1;
        /* A list keeps the highlighted position: guests may sit before row i. */
        f->selected = d->kind == RV_SCREEN_KIND_LIST
                          ? osos_selection_index(osos_ui_selection())
                          : i;
        switch (d->kind) {
        case RV_SCREEN_KIND_LIST:
            rvx_act(&f->ctx, d->list.rows + i);
            break;
        case RV_SCREEN_KIND_CHOICE:
            rvx_set_int(f->ctx.self, d->choice.key, i);
            rvx_pop(f->ctx.self);
            break;
        case RV_SCREEN_KIND_MULTI_CHOICE:
            rvx_set_int(f->ctx.self, d->choice.key,
                        rvx_get_int(f->ctx.self, d->choice.key) ^ (1u << i));
            notify(rows);
            break;
        case RV_SCREEN_KIND_DYNAMIC_LIST:
            if (d->dynamic.select && d->dynamic.select(&f->ctx, i) && rvx_top == f)
                rvx_pop(f->ctx.self);
            break;
        }
    }
    return 1;
}
