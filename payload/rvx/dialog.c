/* SPDX-License-Identifier: GPL-3.0-only */

/* Dialog screens run on the controller class of the stock EU Volume Limit
 * question (native_layout.c), with the frame's copy of its vtable. Activation
 * highlights the declared button after Apple's, which highlights the second.
 * Select reaches the declaration's chosen instead of Apple's apply, which would
 * set the volume limit. */

#include "native_ui.h"
#include "osos.h"
#include "patch.h"

static uintptr_t stock_activate, stock_event; /* Apple's, the same for every dialog */

static struct frame *frame_of(void *controller)
{
    for (struct frame *f = rvx_top; f; f = f->parent)
        if (f->ctx.controller == controller)
            return f;
    return NULL;
}

int rvx_dialog_valid(const struct screen *d)
{
    return d->dialog.message && d->dialog.buttons && d->dialog.count &&
           d->dialog.count <= OptionBarView_MaxSegments &&
           d->dialog.initial < d->dialog.count;
}

/* The dialog on top, after Apple's activation. */
static void highlight(u32 index)
{
    void *model = osos_ui_selection();
    void *(*find)(void *, u32) =
        (void *(*)(void *, u32))osos_method(model, SelectionModel_find_view_method);
    void *view = find(model, RES_DialogNotice_SettingsOptions);
    void *bar =
        view ? NATIVE_CALL(Object_GetInterface)(view, Interface_OptionBar) : NULL;
    if (bar)
        NATIVE_CALL(OptionBarView_SetSelected)(bar, index);
}

static PATCH_ARM int activate(void *controller, u32 argument)
{
    int result = ((int (*)(void *, u32))stock_activate)(controller, argument);
    struct frame *f = frame_of(controller);
    if (f)
        highlight(f->screen->dialog.initial);
    return result;
}

/* Select's release, as EULimitConfirmation_HandleEvent tests it. */
static PATCH_ARM int event(void *controller, void *e)
{
    struct frame *f = frame_of(controller);
    if (!f) /* not Apple's handler: its apply would set the volume limit */
        return osos_controller_handle_event(controller, e);
    const u32 *w = e;
    if (w[1] != 0xd || osos_button_ui_key(e) != 0x6f)
        return ((int (*)(void *, void *))stock_event)(controller, e);
    int pop = 1;
    if (f->screen->dialog.chosen)
        pop = f->screen->dialog.chosen(&f->ctx,
                                       osos_selection_index(osos_ui_selection()));
    if (pop && frame_of(controller) == f)
        NATIVE_CALL(ScreenController_PopSelf)(controller, 0x10);
    return 1;
}

void rvx_dialog_attach(struct frame *f)
{
    stock_activate = f->native_methods[ScreenController_activate_method / 4];
    stock_event = f->native_methods[ScreenController_event_method / 4];
    f->stock_methods[3 + ScreenController_activate_method / 4] = (uintptr_t)activate;
    f->stock_methods[3 + ScreenController_event_method / 4] = (uintptr_t)event;
}
