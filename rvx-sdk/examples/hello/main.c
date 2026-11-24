/* SPDX-License-Identifier: GPL-3.0-only */

/* The first extension of docs/sdk/README.md: a Settings row after Legal that
 * opens a text page. */

#include "rv_api.h"
#include "rv_menus.h"
#include "rv_ui.h"

static const struct rv_api *api;
static rv_extension_handle self;

static const char *const about[] = {"Hello from an extension."};

static const struct rv_screen screens[] = {
    RV_SCREEN_TEXT("about", "Hello", about),
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_LEGAL, RV_ROW_PUSH("Hello", "about"),
                  .pane = RV_PANE(.caption = "Says hello")),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_SCREENS(screens),
    RV_ENTRIES(entries),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core,
                                            rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &extension;
}
