/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_settings.h"
#include "osos.h"
#include "patch.h"

int cfw_settings_action(void *, const char *, uint32_t);

/* Refresh before the Settings main menu builds its visible rows. Its screen
 * uses CFWSettingsMain so the other screens that share the native
 * TSilverSettingsMenuListCntlr class (About, Language, Date & Time and the EU
 * volume limit) leave the Theme row alone. */
static PATCH_ARM uint32_t main_activate(void *controller, uint32_t entering)
{
    if (entering)
        cfw_theme_settings_refresh();
    typedef uint32_t (*activate_fn)(void *, uint32_t);
    return ((activate_fn)0x083a4878)(controller, entering);
}

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

/* The Theme and Accent pickers share one vtable; the Accent controller is
 * remembered at creation so activation can highlight the right row. */
static void *accent_controller;

static PATCH_ARM uint32_t theme_activate(void *controller, uint32_t entering)
{
    return activate_selecting(controller, entering,
                              controller == accent_controller
                                  ? cfw_theme_settings_accent_selected()
                                  : cfw_theme_settings_selected());
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
    if (controller) {
        *(const uintptr_t **)controller = theme_vtable + 3;
        /* A reused address no longer belongs to a deleted Accent controller. */
        if (controller == accent_controller)
            accent_controller = 0;
    }
    return controller;
}

/* The picker's view still renders its rows for the pop transition after its
 * controller is deleted, so the list is released with the Settings main
 * controller instead. 0x08217f84 is the native deleting destructor. */
static PATCH_ARM void *main_destroy(void *controller)
{
    cfw_theme_settings_close();
    typedef void *(*destroy_fn)(void *);
    return ((destroy_fn)0x08217f84)(controller);
}

static const uintptr_t main_vtable[(12 + 0x128) / 4] = {
    [(12 + 4) / 4] = (uintptr_t)main_destroy,
    [(12 + 0xec) / 4] = (uintptr_t)main_activate,
    [(12 + 0x114) / 4] = (uintptr_t)cfw_settings_action,
};
PATCH_COPY(main_vtable, 0, 0x089a3ae0 - 12, 12 + 4);
PATCH_COPY(main_vtable, 12 + 8, 0x089a3ae0 + 8, 0xec - 8);
PATCH_COPY(main_vtable, 12 + 0xf0, 0x089a3ae0 + 0xf0, 0x114 - 0xf0);
PATCH_COPY(main_vtable, 12 + 0x118, 0x089a3ae0 + 0x118, 0x128 - 0x118);

static PATCH_ARM void *main_create(const char *name, uint32_t screen, uint32_t layout,
                                   uint32_t transition)
{
    void *controller = theme_create(name, screen, layout, transition);
    if (controller)
        *(const uintptr_t **)controller = main_vtable + 3;
    return controller;
}

static PATCH_ARM void *accent_create(const char *name, uint32_t screen, uint32_t layout,
                                     uint32_t transition)
{
    void *controller = theme_create(name, screen, layout, transition);
    if (controller)
        accent_controller = controller;
    return controller;
}

/* TDiskModeCntlr exists only while the host owns the volume, which may change
 * /themes, so creating it marks the custom package probe stale. Both factories it
 * registers (0x0813921c) are wrapped: the resource screen subclasses the plain
 * controller with a vtable of its own. Settings cannot open before disk mode
 * ends. */
static PATCH_ARM void *disk_mode_create(uint32_t factory, uint32_t a, uint32_t b,
                                        uint32_t c, uint32_t d)
{
    cfw_theme_settings_invalidate();
    typedef void *(*create_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
    return ((create_fn)factory)(a, b, c, d);
}

PATCH_ARM void *cfw_theme_disk_mode_create(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    return disk_mode_create(0x0814e570, a, b, c, d);
}
PATCH_POINTER(0x08139280, 0x0814e570, cfw_theme_disk_mode_create);

PATCH_ARM void *cfw_theme_disk_mode_resource_create(uint32_t a, uint32_t b, uint32_t c,
                                                    uint32_t d)
{
    return disk_mode_create(0x0814e7d8, a, b, c, d);
}
PATCH_POINTER(0x0813927c, 0x0814e7d8, cfw_theme_disk_mode_resource_create);

/* TMusicLoadingTask (vtable 0x08994174) reloads the music library when disk mode
 * ends; boot loads it elsewhere. Probing after its load reads /themes while the
 * disk is active, so the next Settings open reads nothing. */
PATCH_ARM uint32_t cfw_theme_music_loading_run(void *task)
{
    typedef uint32_t (*run_fn)(void *);
    uint32_t result = ((run_fn)0x081aec04)(task);
    cfw_theme_settings_probe_stale();
    return result;
}
PATCH_POINTER(0x08994174 + 8, 0x081aec04, cfw_theme_music_loading_run);

void cfw_theme_settings_register(void)
{
    osos_ui_register_controllers(osos_controller_registry(), "CFWSettingsMain", 0,
                                 main_create);
    osos_ui_register_controllers(osos_controller_registry(), "CFWAccent", 0, accent_create);
    osos_ui_register_controllers(osos_controller_registry(), "CFWTheme", 0,
                                 theme_create);
}
