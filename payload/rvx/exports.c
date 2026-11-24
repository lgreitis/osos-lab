/* SPDX-License-Identifier: GPL-3.0-only */
#include "host.h"
#include "internal.h"

const struct rvx_host rvx_exports __attribute__((used, section(".rvx.exports"))) = {
    .boot = cfw_rvx_boot,
    .register_ui = cfw_rvx_register_ui,
    .flush = cfw_rvx_flush,
    .reset = cfw_rvx_reset,
    .hold = cfw_rvx_hold,
    .row = cfw_rvx_menu_row,
    .events = cfw_rvx_menu_events,
    .video = cfw_rvx_video_menu,
    .property = cfw_rvx_property,
    .visible = cfw_rvx_visible_rows,
    .visibility = cfw_rvx_visible_property,
    .toggle = cfw_rvx_toggle_row,
    .dispatch = rvx_dispatch,
    .capability_magic = "RepriseRVX1",
    .api = RVX_API,
    .firmware_id = RVX_FIRMWARE_ID,
};
