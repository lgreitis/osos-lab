/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "theme_package.h"

/* Logical preference state, not a disk format. Empty id means Classic.
 * Only CFW_THEME_ACCENT_DEFAULT is accepted. */
struct cfw_theme_selection {
    char id[32];
    uint32_t dark, accent;
};

struct cfw_theme_preferences {
    uint32_t version, pending;
    struct cfw_theme_selection selection;
};

enum cfw_theme_preferences_status {
    CFW_THEME_PREF_ABSENT,
    CFW_THEME_PREF_VALID,
    CFW_THEME_PREF_INVALID
};

enum cfw_theme_store_result {
    CFW_THEME_STORE_DURABLE,
    CFW_THEME_STORE_UNCHANGED,
    CFW_THEME_STORE_UNKNOWN
};

enum cfw_theme_result {
    CFW_THEME_OK,
    CFW_THEME_BUSY,
    CFW_THEME_INVALID,
    CFW_THEME_LOAD_FAILED,
    CFW_THEME_STORE_FAILED
};

/* Called synchronously after package preparation, before changing artwork.
 * A failed write leaves live artwork unchanged, even if disk state is uncertain.
 * Callers serialize UI actions; store must not reenter this switcher. */
struct cfw_theme_switch_ops {
    enum cfw_theme_store_result (*store)(void *context,
                                         const struct cfw_theme_preferences *state);
};

struct cfw_theme_active {
    struct cfw_theme_selection selection;
    struct cfw_theme_style style;
    int custom;
};

struct cfw_theme_switch {
    const struct cfw_theme_switch_ops *ops;
    void *context;
    int busy;
    enum cfw_theme_result result;
    struct cfw_theme_active active;
    uint32_t assets[CFW_THEME_BITMAP_WORDS];
};

int cfw_theme_selection_valid(const struct cfw_theme_selection *selection);
/* Initialize only with stock artwork. The switcher owns all artwork replacements. */
int cfw_theme_switch_init(struct cfw_theme_switch *c,
                          const struct cfw_theme_switch_ops *ops, void *context);
/* Only absent preferences permit CFWDark migration. Invalid or incomplete records
 * recover to Classic light. The result is a candidate, not active artwork. */
int cfw_theme_boot_selection(enum cfw_theme_preferences_status status,
                             const struct cfw_theme_preferences *saved,
                             uint32_t legacy_dark, struct cfw_theme_selection *desired);
/* Persist Classic light only when stock artwork is active. */
int cfw_theme_switch_store_classic(struct cfw_theme_switch *c);
/* Validate and stage, save selection, apply artwork/style, release staging.
 * Returns success synchronously. Caller refreshes cached icons and invalidates
 * the UI on success, just as for Dark Mode.
 * Run on the UI task; queued repeat actions are suppressed by theme_input.
 * Classic light has no pack; Classic dark uses RTH3. */
int cfw_theme_switch_apply(struct cfw_theme_switch *c,
                           const struct cfw_theme_selection *selection,
                           const struct osos_path *pack,
                           const struct osos_path *baseline);

/* Restore an already saved selection from stock at boot, without rewriting it. */
int cfw_theme_switch_restore(struct cfw_theme_switch *c,
                             const struct cfw_theme_selection *selection,
                             const struct osos_path *pack,
                             const struct osos_path *baseline);
