/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_settings.h"
#include "osos.h"
#include "patch.h"

int cfw_settings_action(void *, const char *, uint32_t);

/* Refresh before the native Settings menu builds its visible rows. The theme
 * picker has its own activation callback and keeps its open list stable. */
PATCH_ARM uint32_t cfw_theme_settings_activate(void *controller, uint32_t entering)
{
    if (entering)
        cfw_theme_settings_refresh();
    typedef uint32_t (*activate_fn)(void *, uint32_t);
    return ((activate_fn)0x083a4878)(controller, entering);
}

PATCH_POINTER(0x089a3bcc, 0x083a4878, cfw_theme_settings_activate);

/* Match TCSettings_EQ's post-layout selection callback (0x08134750). */
static PATCH_ARM uint32_t theme_ready(void *controller, uint32_t state, uint32_t index)
{
    (void)controller;
    if (state)
        return 1;
    void *selection = osos_ui_selection();
    typedef void *(*find_fn)(void *, uint32_t);
    void *view = ((find_fn)osos_method(selection, 0xf8))(selection, 0x0dad01b3);
    if (view) {
        typedef void *(*interface_fn)(void *, uint32_t);
        void *list = ((interface_fn)0x0826c594)(view, 0x1700);
        if (list) {
            typedef void (*select_fn)(void *, uint32_t);
            ((select_fn)osos_method(list, 0x174))(list, index);
        }
    }
    return 1;
}

/* The resource Settings controller builds its list during activation. */
static PATCH_ARM __attribute__((noinline)) uint32_t
activate_selecting(void *controller, uint32_t entering, uint32_t index)
{
    typedef uint32_t (*activate_fn)(void *, uint32_t);
    uint32_t result = ((activate_fn)0x083a4878)(controller, entering);
    if (entering)
        theme_ready(controller, 0, index);
    return result;
}

static PATCH_ARM uint32_t theme_activate(void *controller, uint32_t entering)
{
    return activate_selecting(controller, entering, cfw_theme_settings_selected());
}

/* Retain the native Settings controller and resource-screen lifecycle. */
static const uintptr_t theme_vtable[(12 + 0x128) / 4] = {
    [(12 + 0xec) / 4] = (uintptr_t)theme_activate,
    [(12 + 0x114) / 4] = (uintptr_t)cfw_settings_action,
};
PATCH_COPY(theme_vtable, 0, 0x089a3ae0 - 12, 12 + 0xec);
PATCH_COPY(theme_vtable, 12 + 0xf0, 0x089a3ae0 + 0xf0, 0x114 - 0xf0);
PATCH_COPY(theme_vtable, 12 + 0x118, 0x089a3ae0 + 0x118, 0x128 - 0x118);

static PATCH_ARM void *theme_create(const char *name, uint32_t screen, uint32_t layout,
                                    uint32_t transition)
{
    typedef void *(*create_fn)(const char *, uint32_t, uint32_t, uint32_t);
    void *controller = ((create_fn)0x0821e4b0)(name, screen, layout, transition);
    if (controller)
        *(const uintptr_t **)controller = theme_vtable + 3;
    return controller;
}

static PATCH_ARM uint32_t accent_activate(void *controller, uint32_t entering)
{
    return activate_selecting(controller, entering, cfw_theme_settings_accent_selected());
}

static const uintptr_t accent_vtable[(12 + 0x128) / 4] = {
    [(12 + 0xec) / 4] = (uintptr_t)accent_activate,
    [(12 + 0x114) / 4] = (uintptr_t)cfw_settings_action,
};
PATCH_COPY(accent_vtable, 0, 0x089a3ae0 - 12, 12 + 0xec);
PATCH_COPY(accent_vtable, 12 + 0xf0, 0x089a3ae0 + 0xf0, 0x114 - 0xf0);
PATCH_COPY(accent_vtable, 12 + 0x118, 0x089a3ae0 + 0x118, 0x128 - 0x118);

static PATCH_ARM void *accent_create(const char *name, uint32_t screen, uint32_t layout,
                                     uint32_t transition)
{
    void *controller = theme_create(name, screen, layout, transition);
    if (controller)
        *(const uintptr_t **)controller = accent_vtable + 3;
    return controller;
}

void cfw_theme_settings_register(void)
{
    osos_ui_register_controllers(osos_controller_registry(), "CFWAccent", 0, accent_create);
    osos_ui_register_controllers(osos_controller_registry(), "CFWTheme", 0,
                                 theme_create);
}
