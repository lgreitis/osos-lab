/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/* The native song record stores sample rate as a positive IEEE-754 float. */
static inline uint32_t osos_media_item_sample_rate(const struct osos_media_item *item)
{
    if (!item->record)
        return 0;
    uint32_t bits = *(const uint32_t *)((const uint8_t *)item->record + 0x74);
    int exponent = (int)((bits >> 23) & 0xff) - 127;
    if (bits >> 31 || exponent < 0 || exponent > 23)
        return 0;
    return ((bits & 0x7fffff) | 0x800000) >> (23 - exponent);
}

static inline int osos_media_item_is_music(struct osos_media_item *item)
{
    struct osos_media_entry *entry = osos_media_item_entry(item);
    if (!entry || (entry->flags[1] & 1) || osos_media_item_has_record_flag_8f_01(item))
        return 0;
    typedef int (*predicate)(struct osos_media_item *);
    if (((predicate)OSOS_MEDIA_ITEM_HAS_KIND_200000)(item))
        return 0;
    return !osos_media_item_has_kind_8(item) && !osos_media_item_has_kind_8062(item);
}
