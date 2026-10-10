/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM
#include "../../../payload/assets.h"
#include "../../../payload/native/bindings.h"

static const unsigned char stock[40], replacement[40], unlisted[40];
struct cfw_bitmap_asset cfw_bitmap_assets[] = {
    {0x0dad012a, replacement, NULL}
};
const uint32_t cfw_bitmap_asset_count = 1;
static unsigned char view[0x168], context[0x48], shared_bitmap[0xb8];
static unsigned char cache[0xd0];
static int dark, draw_available, loads, attachments;
static int resource_lookups;
static int visible = 1;

static void put_pointer(void *base, unsigned offset, const void *pointer)
{
    memcpy((unsigned char *)base + offset, &pointer, sizeof(pointer));
}

static void *get_pointer(void *base, unsigned offset)
{
    void *pointer;
    memcpy(&pointer, (unsigned char *)base + offset, sizeof(pointer));
    return pointer;
}

int cfw_dark_mode_enabled(void) { return dark; }

void cfw_bitmap_view_load_original(void *object)
{
    assert(object == view);
    if (view[0xa1])
        return;
    if (!visible)
        return;
    void *bitmap = view + 0xa4;
    if (*(uint32_t *)(view + 0x40) == 0x44726177 && draw_available)
        bitmap = shared_bitmap;
    uint32_t id = *(uint32_t *)(view + 0x44);
    put_pointer(bitmap, 4, (id == 123 ? unlisted : stock) + 28);
    put_pointer(context, 0x1c, bitmap);
    put_pointer(view, 0x15c, context);
    view[0xa1] = 1;
}

static void *get_resource(void *service, uint32_t type, uint32_t id)
{
    resource_lookups++;
    (void)service;
    if (id == 123)
        return (void *)unlisted;
    assert(id == 0x0dad012a || id == 0x8c02);
    if (id == 0x8c02 && !visible)
        return NULL;
    if (type == 0x44726177)
        return draw_available ? shared_bitmap : NULL;
    assert(type == 0x424d6170);
    return (void *)stock;
}

void *cfw_bitmap_resource_get_original(void *service, uint32_t type, uint32_t id)
{
    return get_resource(service, type, id);
}

const void *cfw_bitmap_resource_get(void *service, uint32_t type, uint32_t id);

void cfw_bitmap_cache_load_original(void *object)
{
    assert(object == cache);
    if (cache[8] || !visible)
        return;
    const void *data = cfw_bitmap_resource_get(NULL, 0x424d6170,
                                              *(uint32_t *)(cache + 4));
    put_pointer(cache + 0xc, 4, (const unsigned char *)data + 28);
    cache[8] = 1;
}

static void load_bitmap(void *bitmap, const void *data, uint32_t a, uint32_t b)
{
    assert((bitmap == view + 0xa4 || bitmap == cache + 0xc) && !a && !b);
    loads++;
    put_pointer(bitmap, 4, (const unsigned char *)data + 28);
}

static void attach_bitmap(void *target, void *bitmap)
{
    assert(target == context);
    attachments++;
    put_pointer(target, 0x1c, bitmap);
}

#undef OSOS_RESOURCE_SERVICE_GET
#undef OSOS_BITMAP_LOAD_RESOURCE
#undef OSOS_GRAPHICS_ATTACH_BITMAP
#define OSOS_RESOURCE_SERVICE_GET get_resource
#define OSOS_BITMAP_LOAD_RESOURCE load_bitmap
#define OSOS_GRAPHICS_ATTACH_BITMAP attach_bitmap
#include "../../../payload/native/graphics.h"
#include "../../../payload/assets.c"

static void check_switching(uint32_t type, int available, uint32_t id)
{
    memset(view, 0, sizeof(view));
    dark = loads = attachments = resource_lookups = 0;
    draw_available = available;
    *(uint32_t *)(view + 0x40) = type;
    *(uint32_t *)(view + 0x44) = id;
    put_pointer(shared_bitmap, 4, stock + 28);
    cfw_bitmap_view_load(view);
    assert(!loads && !attachments && !resource_lookups);
    void *original = get_pointer(context, 0x1c);
    for (int cycle = 0; cycle < 2; cycle++) {
        dark = 1;
        cfw_bitmap_view_load(view);
        assert(get_pointer(context, 0x1c) == view + 0xa4);
        assert(get_pointer(view + 0xa4, 4) == replacement + 28);
        int previous = attachments;
        cfw_bitmap_view_load(view);
        assert(attachments == previous);
        /* Recreating a Draw cache leaves old embedded pixels behind. */
        view[0xa1] = 0;
        cfw_bitmap_view_load(view);
        assert(get_pointer(context, 0x1c) == view + 0xa4);
        assert(get_pointer(view + 0xa4, 4) == replacement + 28);
        dark = 0;
        cfw_bitmap_view_load(view);
        assert(get_pointer(context, 0x1c) == original);
        assert(get_pointer(original, 4) == stock + 28);
        previous = attachments;
        cfw_bitmap_view_load(view);
        assert(attachments == previous);
        assert(get_pointer(shared_bitmap, 4) == stock + 28);
    }
    int previous = attachments;
    *(uint32_t *)(view + 0x44) = 123;
    view[0xa1] = 0;
    dark = 1;
    cfw_bitmap_view_load(view);
    assert(attachments == previous);
    assert(get_pointer(get_pointer(context, 0x1c), 4) == unlisted + 28);
}

static void check_dynamic_visibility(void)
{
    memset(view, 0, sizeof(view));
    *(uint32_t *)(view + 0x40) = 0x424d6170;
    *(uint32_t *)(view + 0x44) = 0x8c02;
    dark = 1;
    visible = loads = attachments = 0;
    cfw_bitmap_view_load(view);
    assert(!view[0xa1] && !loads && !attachments);
    visible = 1;
    cfw_bitmap_view_load(view);
    assert(get_pointer(view + 0xa4, 4) == replacement + 28);
    visible = view[0xa1] = 0;
    int previous = attachments;
    cfw_bitmap_view_load(view);
    assert(!view[0xa1] && attachments == previous);
    visible = 1;
}

static void check_cached_images(void)
{
    for (int initial_dark = 0; initial_dark < 2; initial_dark++) {
        memset(cache, 0, sizeof(cache));
        *(uint32_t *)(cache + 4) = 0x0dad012a;
        dark = initial_dark;
        cfw_bitmap_cache_load(cache);
        assert(get_pointer(cache + 0xc, 4) == (dark ? replacement : stock) + 28);
        for (int cycle = 0; cycle < 4; cycle++) {
            dark = !dark;
            cfw_bitmap_cache_load(cache);
            assert(get_pointer(cache + 0xc, 4) == (dark ? replacement : stock) + 28);
            int previous = loads;
            cfw_bitmap_cache_load(cache);
            assert(loads == previous);
        }
    }
    memset(cache, 0, sizeof(cache));
    *(uint32_t *)(cache + 4) = 123;
    dark = 1;
    cfw_bitmap_cache_load(cache);
    assert(get_pointer(cache + 0xc, 4) == unlisted + 28);
    dark = 0;
    cfw_bitmap_cache_load(cache);
    assert(get_pointer(cache + 0xc, 4) == unlisted + 28);
    cache[8] = visible = 0;
    dark = 1;
    cfw_bitmap_cache_load(cache);
    assert(!cache[8]);
    visible = 1;
}

static void check_dynamic_dark_source(void)
{
    memset(view, 0, sizeof(view));
    *(uint32_t *)(view + 0x40) = 0x44726177;
    *(uint32_t *)(view + 0x44) = 0x8c02;
    view[0xa1] = 1;
    put_pointer(view, 0x15c, context);
    put_pointer(context, 0x1c, shared_bitmap);
    put_pointer(shared_bitmap, 4, replacement + 28);
    draw_available = 1;
    dark = 0;
    cfw_bitmap_view_load(view);
    assert(get_pointer(context, 0x1c) == view + 0xa4);
    assert(get_pointer(view + 0xa4, 4) == stock + 28);
    assert(get_pointer(shared_bitmap, 4) == replacement + 28);
}

int main(void)
{
    dark = 0;
    assert(cfw_bitmap_resource_get(NULL, 0x424d6170, 0x0dad012a) == stock);
    dark = 1;
    assert(cfw_bitmap_resource_get(NULL, 0x424d6170, 0x0dad012a) == replacement);
    assert(cfw_bitmap_resource_get(NULL, 0x424d6170, 123) == unlisted);
    draw_available = 1;
    assert(cfw_bitmap_resource_get(NULL, 0x44726177, 0x0dad012a) == shared_bitmap);
    check_switching(0x44726177, 1, 0x0dad012a);
    check_switching(0x44726177, 0, 0x0dad012a);
    check_switching(0x424d6170, 0, 0x0dad012a);
    check_switching(0x44726177, 1, 0x8c02);
    check_switching(0x44726177, 0, 0x8c02);
    check_switching(0x424d6170, 0, 0x8c02);
    check_dynamic_visibility();
    check_cached_images();
    check_dynamic_dark_source();
    return 0;
}
