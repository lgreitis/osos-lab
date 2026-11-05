/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include "theme_style.h"

/* Fixed bounds are metadata only; no resident copy of theme artwork. */
#define CFW_THEME_MAX_ASSETS 1024u
#define CFW_THEME_BITMAP_WORDS (CFW_THEME_MAX_ASSETS / 32)
struct osos_path;

struct cfw_theme_metadata {
    uint32_t magic;
    unsigned char catalog[32];
    uint32_t appearance_mask;
    char id[32], name[48];
};

struct cfw_theme_pack {
    unsigned char *data;
    uint32_t size, count, records_offset;
};

struct cfw_theme_transaction {
    struct cfw_theme_pack patch;
    struct cfw_theme_style style;
    uint32_t target_assets[CFW_THEME_BITMAP_WORDS];
    int custom, dark, applied;
};

int cfw_theme_metadata_read(const struct osos_path *path,
                            struct cfw_theme_metadata *out);
int cfw_theme_pack_read(const struct osos_path *path, int dark, int legacy,
                        struct cfw_theme_pack *out);
void cfw_theme_pack_release(struct cfw_theme_pack *pack);
/* target must be a validated pack returned by cfw_theme_pack_read, or NULL for
 * original stock artwork. previous points to CFW_THEME_BITMAP_WORDS entries
 * describing the active theme's replacements; it must not be NULL.
 * The caller owns target and may release it after prepare returns. out must not
 * own an existing transaction. No resident bitmap changes occur during prepare.
 * baseline supplies original artwork for previous replacements absent in target.
 * style/custom/dark describe the target; they are not applied by this API.
 * The caller must serialize rendering, apply/restore style and appearance, and
 * coordinate persistence. Do not combine this with the legacy assets writer. */
int cfw_theme_transaction_prepare(struct cfw_theme_pack *target, int dark,
                                  const uint32_t *previous,
                                  const struct osos_path *baseline,
                                  struct cfw_theme_transaction *out);
/* Call only after successful prepare. Swaps bitmap bytes only; a second call
 * restores them. Release frees staging storage without applying or undoing it. */
void cfw_theme_transaction_swap(struct cfw_theme_transaction *transaction);
void cfw_theme_transaction_release(struct cfw_theme_transaction *transaction);
