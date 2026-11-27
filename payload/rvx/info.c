/* SPDX-License-Identifier: GPL-3.0-only */

/* Settings > CFW Info, after Legal: a text page with the version, the build
 * and the credits, and the row's icon pane. The runtime installs the row and
 * the screen with no package as their owner. The system font has no "+", so
 * the build passes the version as text without one. */

#include "screen.h"
#include "../../rvx-sdk/osos-2.0.5.h"
#include "../../rvx-sdk/rv_api.h"

static const char *const lines[] = {
    "RepriseOS " CFW_VERSION_TEXT,
    "Custom firmware for iPod.",
    "Build: " CFW_REVISION,
    "Project:\nhttps://github.com/lgreitis/osos-lab",
    "Report issues:\nhttps://github.com/lgreitis/osos-lab/issues",
    "Back up your data before installing or updating.",
    "Provided \"as is\", without warranty of any kind.",
    "Credits",
    "RepriseOS contributors:\nlgreitis\nalexballvideos-netizen\nRaduTek\nSwimming33\n"
    "brilliant-alien\nkayboll",
    "Rockbox contributors - bootloader, dual-boot installer, hardware drivers.\n"
    "https://github.com/Rockbox/rockbox",
    "wInd3x by Serge \"q3k\" Bazanski - BootROM exploit and DFU tooling, including "
    "code ported from Go to Rust.\nhttps://github.com/freemyipod/wInd3x",
    "giek2000 - iPod firmware research and inspiration for RepriseOS.\n"
    "https://github.com/giek2000/ipod-classic-firmware-research",
};

static const struct bitmap icon = {"settings-info"};
static const struct pane pane = {.label = "RepriseOS", .bitmap = &icon};

const struct screen rvx_core_screens[] = {
    {.id = "info",
     .title = "CFW Info",
     .kind = RV_SCREEN_KIND_TEXT,
     .text = {lines, sizeof lines / sizeof lines[0]}},
};
const u32 rvx_core_screen_count = sizeof rvx_core_screens / sizeof rvx_core_screens[0];

const struct menu_entry rvx_core_entries[] = {
    {.menus = RV_MENU_BIT(RV_MENU_SETTINGS),
     .after = RES_SettingsMenu_Legal_Item,
     .row = {.label = "CFW Info", .kind = RV_ROW_KIND_PUSH, .screen = "info"},
     .pane = &pane},
};
const u32 rvx_core_entry_count = sizeof rvx_core_entries / sizeof rvx_core_entries[0];
