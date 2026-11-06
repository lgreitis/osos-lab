/* SPDX-License-Identifier: GPL-3.0-only */
#include "catalog_bitmaps.h"
#include "cfw.h"

enum { RUN_LITERALS = 0, RUN_ZEROS = 1, RUN_REPEAT = 2, RUN_MAX = 64 };

/* Position + 1 of the bitmap in the slot; 0 while the slot holds none. */
static uint32_t held;

static int unpack(const struct cfw_catalog_source *source, unsigned char *out)
{
    const unsigned char *in = source->data;
    uint32_t available = source->size, left = cfw_catalog_pixel_bytes;
    while (available) {
        uint32_t kind = *in >> 6, count = (*in & 63u) + 1;
        in++;
        available--;
        if (count > left || kind > RUN_REPEAT)
            return 0;
        if (kind == RUN_LITERALS) {
            if (available < count)
                return 0;
            cfw_copy(out, in, count);
            in += count;
            available -= count;
        } else if (kind == RUN_ZEROS) {
            cfw_zero(out, count);
        } else {
            if (!available)
                return 0;
            for (uint32_t i = 0; i < count; i++)
                out[i] = *in;
            in++;
            available--;
        }
        out += count;
        left -= count;
    }
    return !left;
}

const unsigned char *cfw_catalog_bitmap(uint32_t position)
{
    if (position >= cfw_catalog_bitmap_count)
        return 0;
    if (held != position + 1) {
        held = 0;
        cfw_copy(cfw_catalog_slot, cfw_catalog_header, CFW_BMAP_HEADER_BYTES);
        if (!unpack(&cfw_catalog_sources[position],
                    cfw_catalog_slot + CFW_BMAP_HEADER_BYTES))
            return 0;
        held = position + 1;
    }
    return cfw_catalog_slot;
}

int cfw_catalog_slot_position(const unsigned char *pixels, uint32_t *position)
{
    if (!held || pixels != cfw_catalog_slot + CFW_BMAP_HEADER_BYTES)
        return 0;
    *position = held - 1;
    return 1;
}

void cfw_catalog_reload(void)
{
    uint32_t position = held;
    held = 0;
    if (position)
        cfw_catalog_bitmap(position - 1);
}

static uint32_t literals(unsigned char *out, uint32_t at, const unsigned char *data,
                         uint32_t count)
{
    if (!count)
        return 0;
    if (out) {
        out[at] = RUN_LITERALS << 6 | (count - 1);
        cfw_copy(out + at + 1, data, count);
    }
    return count + 1;
}

/* Matches pack_runs in patching/bitmap.py: a run of at least three equal bytes,
 * or two zeros, ends the pending literals. */
uint32_t cfw_catalog_pack(const unsigned char *pixels, unsigned char *out)
{
    uint32_t size = cfw_catalog_pixel_bytes, written = 0, start = 0, pending = 0;
    for (uint32_t at = 0; at < size;) {
        unsigned char value = pixels[at];
        uint32_t run = 1;
        while (run < RUN_MAX && at + run < size && pixels[at + run] == value)
            run++;
        if (run >= 3 || (run == 2 && !value)) {
            written += literals(out, written, pixels + start, pending);
            pending = 0;
            if (out) {
                out[written] = (value ? RUN_REPEAT : RUN_ZEROS) << 6 | (run - 1);
                if (value)
                    out[written + 1] = value;
            }
            written += value ? 2 : 1;
            at += run;
            start = at;
        } else {
            if (pending == RUN_MAX) {
                written += literals(out, written, pixels + start, pending);
                pending = 0;
                start = at;
            }
            pending++;
            at++;
        }
    }
    return written + literals(out, written, pixels + start, pending);
}
