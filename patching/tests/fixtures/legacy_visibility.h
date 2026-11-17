/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

static inline struct cfw_ui_visibility_file cfw_ui_visibility_encode(uint32_t key,
                                                                     uint32_t visible)
{
    return (struct cfw_ui_visibility_file){0x554e4d43, 2, key, visible,
                                           ~(key ^ visible)};
}
