/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

struct cfw_ui_visibility {
    uint32_t key, title, items[2], anchors[2], defaults;
    const char *filename;
    uint32_t *visible;
};
extern const struct cfw_ui_visibility cfw_ui_visibility[];
extern const uint32_t cfw_ui_visibility_count;

enum { CFW_UI_MAIN_MENU, CFW_UI_MUSIC_MENU };

#define CFW_UI_CUSTOM_ROW 0x80000000u

/* Native settings rows are five words; the first word is the menu item ID. */
static inline uint32_t cfw_ui_visibility_index(unsigned menu, uint32_t index,
                                               const uint32_t *rows, uint32_t count)
{
    uint32_t displayed = 0;
    for (uint32_t native = 0; native < count; native++) {
        if (displayed++ == index)
            return native;
        for (uint32_t i = 0; i < cfw_ui_visibility_count; i++) {
            if (cfw_ui_visibility[i].anchors[menu] != rows[native * 5])
                continue;
            if (displayed++ == index)
                return CFW_UI_CUSTOM_ROW | i;
        }
    }
    return UINT32_MAX;
}

static inline uint32_t
cfw_ui_visibility_extra_count(unsigned menu, const uint32_t *rows, uint32_t count)
{
    uint32_t extra = 0;
    for (uint32_t native = 0; native < count; native++)
        for (uint32_t i = 0; i < cfw_ui_visibility_count; i++)
            extra += cfw_ui_visibility[i].anchors[menu] == rows[native * 5];
    return extra;
}

struct cfw_ui_visibility_file {
    uint32_t magic, version, key, visible, check;
};

static inline struct cfw_ui_visibility_file cfw_ui_visibility_encode(uint32_t key,
                                                                     uint32_t visible)
{
    return (struct cfw_ui_visibility_file){0x554e4d43, 2, key, visible,
                                           ~(key ^ visible)};
}

static inline int cfw_ui_visibility_valid(const struct cfw_ui_visibility_file *file,
                                          uint32_t key)
{
    return file->magic == 0x554e4d43 && file->version == 2 && file->key == key &&
           file->visible <= 3 && file->check == ~(key ^ file->visible);
}

void cfw_ui_visibility_adjust_property(uint32_t type, uint32_t property,
                                       uintptr_t *out);
int cfw_ui_visibility_property(void *model, uint32_t type, uint32_t property,
                               uint32_t index, uintptr_t *out);
