/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_switch.h"
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
    struct cfw_theme_pack pack;
    struct cfw_theme_transaction transaction;
    cfw_zero(&pack, sizeof(pack));
    cfw_zero(&transaction, sizeof(transaction));
    c->result = CFW_THEME_LOAD_FAILED;
    if (path && !cfw_theme_pack_read(path, selection->dark, !selection->id[0], &pack))
        goto done;
    if (!cfw_theme_transaction_prepare(path ? &pack : 0, selection->dark, c->assets,
                                       baseline, &transaction))
        goto done;
    cfw_theme_pack_release(&pack);
    c->result = CFW_THEME_STORE_FAILED;
    if (persist && !store_selection(c, selection))
        goto done;
    cfw_theme_transaction_swap(&transaction);
    cfw_copy(&c->active.selection, selection, sizeof(*selection));
    c->active.custom = transaction.custom;
    cfw_copy(c->assets, transaction.target_assets, sizeof(c->assets));
    if (c->active.custom) {
        const unsigned char *prefix = transaction.incoming.data;
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
done:
    cfw_theme_pack_release(&pack);
    cfw_theme_transaction_release(&transaction);
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
