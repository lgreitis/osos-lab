/* SPDX-License-Identifier: GPL-3.0-only */
#include "rv_api.h"
#include "rv_ui.h"

static const struct rv_api *api;

static uint32_t offset(uint32_t zone, const void *next)
{
    return api->hook_next(next, zone, 0, 0, 0, 0, 0) + 1;
}

static uint32_t thumb(uint32_t image, uint32_t format, uint32_t load, uint32_t index,
                      const void *next)
{
    if (image == 0x525658)
        return format + 2 * load + 3 * index;
    return api->hook_next(next, image, format, load, index, 0, 0);
}

static struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_RAW_SITE(0), offset),
    RV_HOOK_FILTER(RV_RAW_SITE(1), thumb),
    RV_HOOK_WORD(RV_RAW_SITE(2), 0),
};
static const struct rv_descriptor descriptor = {.size = sizeof descriptor,
                                                RV_HOOKS(hooks)};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core,
                                            rv_extension_handle self)
{
    if (!self || !RV_HAS(core, hook_next))
        return 0;
    api = core;
    hooks[2].word = 0x6000 | (core->version & 0xff);
    return &descriptor;
}
