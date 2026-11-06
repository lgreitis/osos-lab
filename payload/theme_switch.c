/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_switch.h"
#include "catalog_bitmaps.h"
#include "cfw.h"
#include "theme_id.h"
#include "theme_input.h"
#include "theme_tint.h"

int cfw_theme_selection_valid(const struct cfw_theme_selection *s)
{
    return s && s->dark <= 1 && s->accent < CFW_THEME_ACCENT_COUNT &&
           cfw_theme_id_field_valid(s->id);
}

int cfw_theme_boot_selection(enum cfw_theme_preferences_status status,
                             const struct cfw_theme_preferences *saved,
                             struct cfw_theme_selection *desired)
{
    cfw_zero(desired, sizeof(*desired));
    if (status == CFW_THEME_PREF_ABSENT)
        return 0;
    if (status != CFW_THEME_PREF_VALID || !saved || saved->version != 1 ||
        saved->pending || !cfw_theme_selection_valid(&saved->selection))
        return 1;
    cfw_copy(desired, &saved->selection, sizeof(*desired));
    return 0;
}

int cfw_theme_switch_init(struct cfw_theme_switch *c,
                          const struct cfw_theme_switch_ops *ops)
{
    if (!c || !ops || !ops->store)
        return 0;
    cfw_zero(c, sizeof(*c));
    c->ops = ops;
    cfw_theme_style_reset();
    cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT);
    return 1;
}

static int store_selection(struct cfw_theme_switch *c,
                           const struct cfw_theme_selection *selection)
{
    struct cfw_theme_preferences state;
    cfw_zero(&state, sizeof(state));
    state.version = 1;
    cfw_copy(&state.selection, selection, sizeof(*selection));
    return c->ops->store(&state) == CFW_THEME_STORE_DURABLE;
}

static int stock_active(const struct cfw_theme_switch *c)
{
    if (c->active.selection.id[0] || c->active.selection.dark || c->active.custom)
        return 0;
    for (unsigned i = 0; i < CFW_THEME_BITMAP_WORDS; i++)
        if (c->assets[i])
            return 0;
    return 1;
}

int cfw_theme_switch_store_classic(struct cfw_theme_switch *c)
{
    if (c->busy || c->active.selection.accent || !stock_active(c))
        return 0;
    c->busy = 1;
    int ok = store_selection(c, &c->active.selection);
    c->busy = 0;
    c->result = ok ? CFW_THEME_OK : CFW_THEME_STORE_FAILED;
    return ok;
}

/* Artwork changed after the selection was saved: restore every bitmap the
 * previous or the failed theme touched and save Classic light. Bitmaps that could
 * not be restored stay marked as replacements. */
static void recover(struct cfw_theme_switch *c, const uint32_t *touched,
                    const struct osos_path *baseline)
{
    uint32_t restore[CFW_THEME_BITMAP_WORDS];
    for (unsigned i = 0; i < CFW_THEME_BITMAP_WORDS; i++)
        restore[i] = c->assets[i] | touched[i];
    int restored = cfw_theme_baseline_restore(baseline, restore);
    cfw_zero(&c->active, sizeof(c->active));
    if (restored)
        cfw_zero(c->assets, sizeof(c->assets));
    else
        cfw_copy(c->assets, restore, sizeof(c->assets));
    cfw_theme_style_reset();
    cfw_theme_appearance_set(0);
    cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT);
    cfw_theme_tint_refresh();
    store_selection(c, &c->active.selection);
    c->result = restored ? CFW_THEME_RECOVERED : CFW_THEME_RESTORE_FAILED;
}

static int switch_run(struct cfw_theme_switch *c,
                      const struct cfw_theme_selection *selection,
                      const struct osos_path *path, const struct osos_path *baseline,
                      int persist)
{
    if (c->busy)
        return 0;
    if (!cfw_theme_selection_valid(selection) || !baseline ||
        (!!path != !!(selection->id[0] || selection->dark))) {
        c->result = CFW_THEME_INVALID;
        return 0;
    }
    if (!cfw_theme_input_begin()) {
        c->result = CFW_THEME_BUSY;
        return 0;
    }
    c->busy = 1;
    struct cfw_theme_reader pack;
    uint32_t style[CFW_THEME_STYLE_BYTES / 4];
    uint32_t target[CFW_THEME_BITMAP_WORDS], restore[CFW_THEME_BITMAP_WORDS];
    cfw_zero(&pack, sizeof(pack));
    cfw_zero(target, sizeof(target));
    /* Recovery needs the baseline, so check it before saving anything. */
    c->result = CFW_THEME_LOAD_FAILED;
    if ((path &&
         !cfw_theme_pack_open(path, selection->dark, &pack, (unsigned char *)style)) ||
        !cfw_theme_baseline_valid(baseline))
        goto done;
    c->result = CFW_THEME_STORE_FAILED;
    if (persist && !store_selection(c, selection))
        goto done;
    int applied = !path || cfw_theme_pack_apply(&pack, (unsigned char *)style, target);
    cfw_theme_reader_close(&pack);
    for (unsigned i = 0; i < CFW_THEME_BITMAP_WORDS; i++)
        restore[i] = c->assets[i] & ~target[i];
    if (!applied || !cfw_theme_baseline_restore(baseline, restore)) {
        recover(c, target, baseline);
    } else {
        cfw_copy(&c->active.selection, selection, sizeof(*selection));
        c->active.custom = selection->id[0] != 0;
        cfw_copy(c->assets, target, sizeof(c->assets));
        if (c->active.custom) {
            const unsigned char *prefix = (const unsigned char *)style;
            cfw_theme_style_set(
                (const struct cfw_theme_style *)prefix,
                (const struct cfw_theme_options *)(prefix +
                                                   sizeof(struct cfw_theme_style)));
        } else {
            cfw_theme_style_reset();
        }
        cfw_theme_appearance_set(selection->dark);
        cfw_theme_accent_set(selection->accent);
        cfw_theme_tint_refresh();
        c->result = CFW_THEME_OK;
    }
    cfw_catalog_reload();
done:
    cfw_theme_reader_close(&pack);
    c->busy = 0;
    cfw_theme_input_finish();
    return c->result == CFW_THEME_OK;
}

int cfw_theme_switch_apply(struct cfw_theme_switch *c,
                           const struct cfw_theme_selection *selection,
                           const struct osos_path *pack,
                           const struct osos_path *baseline)
{
    return switch_run(c, selection, pack, baseline, 1);
}

int cfw_theme_switch_restore(struct cfw_theme_switch *c,
                             const struct cfw_theme_selection *selection,
                             const struct osos_path *pack,
                             const struct osos_path *baseline)
{
    /* Restoration is only for startup, before any theme has replaced stock. */
    return stock_active(c) && switch_run(c, selection, pack, baseline, 0);
}

int cfw_theme_switch_set_accent(struct cfw_theme_switch *c, unsigned accent)
{
    if (c->busy)
        return 0;
    struct cfw_theme_selection selection;
    cfw_copy(&selection, &c->active.selection, sizeof(selection));
    selection.accent = accent;
    if (!cfw_theme_selection_valid(&selection)) {
        c->result = CFW_THEME_INVALID;
        return 0;
    }
    if (!cfw_theme_input_begin()) {
        c->result = CFW_THEME_BUSY;
        return 0;
    }
    c->busy = 1;
    c->result = CFW_THEME_STORE_FAILED;
    if (store_selection(c, &selection)) {
        c->active.selection.accent = accent;
        cfw_theme_accent_set(accent);
        cfw_theme_tint_refresh();
        c->result = CFW_THEME_OK;
    }
    c->busy = 0;
    cfw_theme_input_finish();
    return c->result == CFW_THEME_OK;
}
