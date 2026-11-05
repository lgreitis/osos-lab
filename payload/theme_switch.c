/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_switch.h"
#include "theme_input.h"

static void switch_copy(void *out, const void *in, unsigned bytes)
{
    unsigned char *p = out;
    const unsigned char *q = in;
    while (bytes--)
        *p++ = *q++;
}

static void switch_zero(void *out, unsigned bytes)
{
    unsigned char *p = out;
    while (bytes--)
        *p++ = 0;
}

int cfw_theme_selection_valid(const struct cfw_theme_selection *s)
{
    if (!s || s->dark > 1 || s->accent != CFW_THEME_ACCENT_DEFAULT)
        return 0;
    static const char *const reserved[] = {"default", "dark"};
    for (unsigned n = 0; n < sizeof(reserved) / sizeof(*reserved); n++) {
        unsigned at = 0;
        while (reserved[n][at] && s->id[at] == reserved[n][at])
            at++;
        if (!reserved[n][at] && !s->id[at])
            return 0;
    }
    unsigned i = 0;
    while (i < sizeof(s->id) && s->id[i]) {
        unsigned char ch = s->id[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
              (i && (ch == '-' || ch == '_'))))
            return 0;
        i++;
    }
    if (i == sizeof(s->id))
        return 0;
    for (; i < sizeof(s->id); i++)
        if (s->id[i])
            return 0;
    return 1;
}

int cfw_theme_boot_selection(enum cfw_theme_preferences_status status,
                             const struct cfw_theme_preferences *saved,
                             uint32_t legacy_dark, struct cfw_theme_selection *desired)
{
    switch_zero(desired, sizeof(*desired));
    if (status == CFW_THEME_PREF_ABSENT) {
        desired->dark = legacy_dark == 0x314b5244u;
        return 0;
    }
    if (status != CFW_THEME_PREF_VALID || !saved || saved->version != 1 ||
        saved->pending || !cfw_theme_selection_valid(&saved->selection))
        return 1;
    switch_copy(desired, &saved->selection, sizeof(*desired));
    return 0;
}

int cfw_theme_switch_init(struct cfw_theme_switch *c,
                          const struct cfw_theme_switch_ops *ops, void *context)
{
    if (!c || !ops || !ops->store)
        return 0;
    switch_zero(c, sizeof(*c));
    c->ops = ops;
    c->context = context;
    cfw_theme_style_reset();
    cfw_theme_accent_set(CFW_THEME_ACCENT_DEFAULT);
    return 1;
}

static int store_selection(struct cfw_theme_switch *c,
                           const struct cfw_theme_selection *selection)
{
    struct cfw_theme_preferences state;
    switch_zero(&state, sizeof(state));
    state.version = 1;
    switch_copy(&state.selection, selection, sizeof(*selection));
    return c->ops->store(c->context, &state) == CFW_THEME_STORE_DURABLE;
}

int cfw_theme_switch_store_classic(struct cfw_theme_switch *c)
{
    if (c->busy || c->active.selection.id[0] || c->active.selection.dark ||
        c->active.selection.accent || c->active.custom)
        return 0;
    for (unsigned i = 0; i < CFW_THEME_BITMAP_WORDS; i++)
        if (c->assets[i])
            return 0;
    c->busy = 1;
    int ok = store_selection(c, &c->active.selection);
    c->busy = 0;
    c->result = ok ? CFW_THEME_OK : CFW_THEME_STORE_FAILED;
    return ok;
}

int cfw_theme_switch_apply(struct cfw_theme_switch *c,
                           const struct cfw_theme_selection *selection,
                           const struct osos_path *path,
                           const struct osos_path *baseline)
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
    switch_zero(&pack, sizeof(pack));
    switch_zero(&transaction, sizeof(transaction));
    c->result = CFW_THEME_LOAD_FAILED;
    if (path && !cfw_theme_pack_read(path, selection->dark, !selection->id[0], &pack))
        goto done;
    if (!cfw_theme_transaction_prepare(path ? &pack : 0, selection->dark, c->assets,
                                       baseline, &transaction))
        goto done;
    cfw_theme_pack_release(&pack);
    c->result = CFW_THEME_STORE_FAILED;
    if (!store_selection(c, selection))
        goto done;
    cfw_theme_transaction_swap(&transaction);
    switch_copy(&c->active.selection, selection, sizeof(*selection));
    switch_copy(&c->active.style, &transaction.style, sizeof(transaction.style));
    c->active.custom = transaction.custom;
    switch_copy(c->assets, transaction.target_assets, sizeof(c->assets));
    if (c->active.custom)
        cfw_theme_style_set(&c->active.style);
    else
        cfw_theme_style_reset();
    cfw_theme_appearance_set(selection->dark);
    cfw_theme_accent_set(selection->accent);
    c->result = CFW_THEME_OK;
done:
    cfw_theme_pack_release(&pack);
    cfw_theme_transaction_release(&transaction);
    c->busy = 0;
    cfw_theme_input_finish();
    return c->result == CFW_THEME_OK;
}
