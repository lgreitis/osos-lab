/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert

struct test_model {
    void *browser;
    uint8_t ownership;
    unsigned kind;
};

struct test_reference {
    struct test_model *model;
    unsigned count;
};

enum { STOCK, UP_NEXT, ALBUM_ARTISTS, FOREIGN };

static int main_browser, secondary_browser, child_slots[4], selection;
static void *registry[4], *selected_reference, *artwork_reference;
static unsigned models_live, references_live, creates, destroys, children, swaps,
    releases;
static int fail_create_at, fail_child, fail_artwork;

static void *model_field(void *object, unsigned offset)
{
    struct test_model *model = object;
    if (offset == 0x40)
        return &model->browser;
    assert(offset == 0x45);
    return &model->ownership;
}

#define FIELD(type, object, offset) (*(type *)model_field(object, offset))

static struct test_model *new_model(void)
{
    struct test_model *model = calloc(1, sizeof(*model));
    assert(model);
    models_live++;
    model->browser = &main_browser;
    return model;
}

static void destroy_model(void *object)
{
    struct test_model *model = object;
    assert(model && models_live);
    if (model->ownership == 2) {
        unsigned i = 0;
        while (i < 4 && registry[i] != model->browser)
            i++;
        assert(i < 4);
        registry[i] = NULL;
        children--;
    }
    models_live--;
    destroys++;
    free(model);
}

static void ref_init(void **out, void *model, int synchronized)
{
    assert(!synchronized && !*out);
    struct test_reference *ref = malloc(sizeof(*ref));
    assert(ref);
    *ref = (struct test_reference){model, 1};
    *out = ref;
    references_live++;
}

static void ref_release(void *address)
{
    void **out = address;
    struct test_reference *ref = *out;
    if (!ref)
        return;
    assert(ref->count);
    *out = NULL;
    if (!--ref->count) {
        destroy_model(ref->model);
        free(ref);
        references_live--;
    }
}

static void ref_assign(void **out, void *source)
{
    struct test_reference *ref = source;
    if (ref)
        ref->count++;
    ref_release(out);
    *out = ref;
}

static void *ref_get(void *address)
{
    struct test_reference *ref = *(void **)address;
    return ref ? ref->model : NULL;
}

static int ref_unique(void *address)
{
    struct test_reference *ref = *(void **)address;
    return ref && ref->count == 1;
}

static void *create_model(uint32_t ownership)
{
    assert(!ownership);
    creates++;
    return fail_create_at && creates == (unsigned)fail_create_at ? NULL : new_model();
}

static int create_child(void *owner, void **out)
{
    assert(owner == &main_browser);
    if (fail_child)
        return -108;
    for (unsigned i = 0; i < 4; i++)
        if (!registry[i]) {
            registry[i] = &child_slots[i];
            *out = registry[i];
            children++;
            return 0;
        }
    assert(!"allocator must reserve a native auxiliary browser slot");
    return -108;
}

static void **browser_registry(void)
{
    return registry;
}

static void *selection_get(void)
{
    return &selection;
}

static void **selection_reference(void *s)
{
    assert(s == &selection);
    return &selected_reference;
}

static void selection_set_list(void *s, void *reference, uint32_t category, uint32_t a,
                               uint8_t b)
{
    assert(s == &selection && category == 1 && a == 0 && b == 0);
    ref_assign(&selected_reference, *(void **)reference);
    swaps++;
}

int cfw_up_next_owns_model(void *model)
{
    return model && ((struct test_model *)model)->kind == UP_NEXT;
}

int cfw_album_artists_owns_model(void *model)
{
    return model && ((struct test_model *)model)->kind == ALBUM_ARTISTS;
}

int cfw_album_artists_release_artwork(void)
{
    releases++;
    if (fail_artwork)
        return 0;
    ref_release(&artwork_reference);
    return 1;
}

#define osos_model_create create_model
#define osos_model_destroy destroy_model
#define osos_browser_create create_child
#define osos_browser_registry browser_registry
#define osos_model_ref_init ref_init
#define osos_model_ref_release ref_release
#define osos_model_ref_get ref_get
#define osos_model_ref_unique ref_unique
#define osos_selection_get selection_get
#define osos_selection_reference selection_reference
#define osos_selection_set_list selection_set_list
#include "../../../payload/media_browser.c"

static void reset(void)
{
    ref_release(&artwork_reference);
    ref_release(&selected_reference);
    assert(!models_live && !references_live && !children);
    memset(registry, 0, sizeof(registry));
    registry[0] = &main_browser;
    registry[1] = &secondary_browser;
    creates = destroys = swaps = releases = 0;
    fail_create_at = fail_child = fail_artwork = 0;
}

static struct test_model *select_custom(unsigned kind)
{
    struct test_model *model = new_model();
    model->kind = kind;
    model->ownership = 2;
    assert(!create_child(&main_browser, &model->browser));
    void *ref = NULL;
    ref_init(&ref, model, 0);
    ref_assign(&selected_reference, ref);
    ref_release(&ref);
    return model;
}

static unsigned free_slots(void)
{
    unsigned result = 0;
    for (unsigned i = 0; i < 4; i++)
        result += !registry[i];
    return result;
}

static void check_handoff(unsigned previous, unsigned next)
{
    reset();
    select_custom(previous);
    assert(free_slots() == 1);
    void *ref = NULL;
    struct test_model *model = cfw_media_browser_create(&ref);
    assert(model && model->ownership == 2 && free_slots() == 1);
    assert(swaps == 1 && destroys == 1 && releases == 1);
    assert(((struct test_model *)ref_get(&selected_reference))->kind == STOCK);
    model->kind = next;
    ref_assign(&selected_reference, ref);
    ref_release(&ref);
    assert(models_live == 1 && references_live == 1 && children == 1);
}

static void check_retained_and_foreign(void)
{
    reset();
    select_custom(UP_NEXT);
    void *active = NULL;
    ref_assign(&active, selected_reference);
    void *ref = NULL;
    assert(!cfw_media_browser_create(&ref) && !ref && !swaps && !destroys);
    assert(((struct test_reference *)active)->count == 2 && free_slots() == 1);
    ref_release(&active);
    reset();
    select_custom(FOREIGN);
    assert(!cfw_media_browser_create(&ref) && !swaps && !destroys && free_slots() == 1);
    reset();
    struct test_model *borrowed = select_custom(UP_NEXT);
    borrowed->ownership = 3;
    assert(!cfw_media_browser_create(&ref) && !swaps && !destroys);
    borrowed->ownership = 2;
    reset();
    select_custom(ALBUM_ARTISTS);
    ref_assign(&artwork_reference, selected_reference);
    assert(!ref_unique(&selected_reference));
    assert(cfw_media_browser_create(&ref) && swaps == 1 && !artwork_reference);
    ref_release(&ref);
}

static void check_failures(void)
{
    void *ref = NULL;
    reset();
    select_custom(ALBUM_ARTISTS);
    ref_assign(&artwork_reference, selected_reference);
    fail_artwork = 1;
    assert(!cfw_media_browser_create(&ref) && !ref && !creates && !swaps &&
           free_slots() == 1);
    fail_artwork = 0;
    reset();
    select_custom(UP_NEXT);
    fail_create_at = 1;
    assert(!cfw_media_browser_create(&ref) && !swaps && !destroys && free_slots() == 1);
    reset();
    select_custom(UP_NEXT);
    fail_create_at = 2;
    assert(!cfw_media_browser_create(&ref) && swaps == 1 && free_slots() == 2);
    assert(models_live == 1 &&
           ((struct test_model *)ref_get(&selected_reference))->kind == STOCK);
    reset();
    fail_child = 1;
    assert(!cfw_media_browser_create(&ref) && !models_live && !references_live &&
           free_slots() == 2);
    reset();
    fail_create_at = 1;
    assert(!cfw_media_browser_create(&ref) && !models_live && free_slots() == 2);
}

int main(void)
{
    check_handoff(UP_NEXT, ALBUM_ARTISTS);
    check_handoff(ALBUM_ARTISTS, UP_NEXT);
    check_retained_and_foreign();
    check_failures();
    reset();
    return 0;
}
