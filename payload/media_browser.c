/* SPDX-License-Identifier: GPL-3.0-only */
#include "media_browser.h"
#include "album_artists.h"
#include "up_next.h"

#ifndef FIELD
#define FIELD(type, object, offset) (*(type *)((uint8_t *)(object) + (offset)))
#endif

static void release_cached_selection(void)
{
    void *selection = osos_selection_get();
    void **reference = osos_selection_reference(selection);
    void *model = osos_model_ref_get(reference);
    if ((!cfw_up_next_owns_model(model) && !cfw_album_artists_owns_model(model)) ||
        FIELD(uint8_t, model, 0x45) != 2 || !osos_model_ref_unique(reference))
        return;
    /* The selection is the sole owner: no screen or artwork still uses this model. */
    void *stock = osos_model_create(0);
    if (!stock)
        return;
    void *replacement = NULL;
    osos_model_ref_init(&replacement, stock, 0);
    osos_selection_set_list(selection, &replacement, 1, 0, 0);
    osos_model_ref_release(&replacement);
}

void *cfw_media_browser_create(void **reference)
{
    if (!cfw_album_artists_release_artwork())
        return NULL;
    release_cached_selection();
    void **registry = osos_browser_registry();
    unsigned available = 0;
    for (unsigned i = 0; i < 4; i++)
        available += registry[i] == NULL;
    if (available < 2)
        return NULL;
    void *model = osos_model_create(0);
    if (!model)
        return NULL;
    void *browser = NULL;
    if (osos_browser_create(FIELD(void *, model, 0x40), &browser) || !browser) {
        osos_model_destroy(model);
        return NULL;
    }
    FIELD(void *, model, 0x40) = browser;
    FIELD(uint8_t, model, 0x45) = 2;
    osos_model_ref_init(reference, model, 0);
    return model;
}
