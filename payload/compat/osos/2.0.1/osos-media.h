/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

static inline int osos_media_item_is_music(struct osos_media_item *item)
{
    struct osos_media_entry *entry = osos_media_item_entry(item);
    if (!entry || (entry->flags[1] & 1) || osos_media_item_has_record_flag_8f_01(item))
        return 0;
    return !osos_media_item_has_kind_8(item) && !osos_media_item_has_kind_8062(item);
}
