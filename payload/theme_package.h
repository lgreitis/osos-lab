/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include "theme_style.h"

/* Upper bound on catalog assets; the generated catalog asserts it. Metadata only:
 * no resident copy of theme artwork. */
#define CFW_THEME_MAX_ASSETS 640u
#define CFW_THEME_BITMAP_WORDS (CFW_THEME_MAX_ASSETS / 32)
struct osos_path;
#define CFW_THEME_STYLE_BYTES (sizeof(struct cfw_theme_style) + sizeof(struct cfw_theme_options))

struct cfw_theme_metadata {
    uint32_t magic;
    unsigned char catalog[32];
    uint32_t appearance_mask;
    char id[32], name[48];
};

struct cfw_theme_pack {
    unsigned char *data;
    uint32_t size, count, records_offset;
    /* Custom packs supply the style; Classic packs keep the built-in style. */
    uint32_t custom;
};

/* A custom incoming pack keeps its style prefix at the start of incoming.data. */
struct cfw_theme_transaction {
    struct cfw_theme_pack incoming, patch;
    uint32_t target_assets[CFW_THEME_BITMAP_WORDS];
    int custom, dark, applied;
};

/* Shared primitives for the RTH4/RTP1 codecs. The CRC runs on an inverted seed:
 * start from UINT32_MAX and invert the result. */
uint32_t cfw_theme_crc32(uint32_t crc, const void *data, uint32_t size);
uint32_t cfw_theme_u32(const unsigned char *p);
/* Volume 0 path from a NUL-terminated name shorter than the path buffer. */
void cfw_theme_path(struct osos_path *out, const char *text);

/* Validate the complete original-artwork baseline without retaining its body. */
int cfw_theme_baseline_valid(const struct osos_path *path);
/* Stream an appearance pack and check its header, style opacity, record layout and
 * checksum without allocating. Palette indices are checked when a pack is read. */
int cfw_theme_pack_valid(const struct osos_path *path, int dark);

int cfw_theme_metadata_read(const struct osos_path *path,
                            struct cfw_theme_metadata *out);
int cfw_theme_pack_read(const struct osos_path *path, int dark, int classic,
                        struct cfw_theme_pack *out);
/* Additional staging bytes for previous replacements absent from target. */
int cfw_theme_transaction_bytes(const struct cfw_theme_pack *target,
                                const uint32_t *previous, uint32_t *bytes);
void cfw_theme_pack_release(struct cfw_theme_pack *pack);
/* target must be a validated pack returned by cfw_theme_pack_read, or NULL for
 * original stock artwork. previous points to CFW_THEME_BITMAP_WORDS entries
 * describing the active theme's replacements; it must not be NULL.
 * Success transfers target storage to out and clears target; failure retains it.
 * out must not own an existing transaction. No resident bitmap changes occur during prepare.
 * baseline supplies original artwork for previous replacements absent in target.
 * style/custom/dark describe the target; they are not applied by this API.
 * The caller must serialize rendering, apply/restore style and appearance, and
 * coordinate persistence. Use a single artwork owner. */
int cfw_theme_transaction_prepare(struct cfw_theme_pack *target, int dark,
                                  const uint32_t *previous,
                                  const struct osos_path *baseline,
                                  struct cfw_theme_transaction *out);
/* Call only after successful prepare. Swaps bitmap bytes only; a second call
 * restores them. Release frees staging storage without applying or undoing it. */
void cfw_theme_transaction_swap(struct cfw_theme_transaction *transaction);
void cfw_theme_transaction_release(struct cfw_theme_transaction *transaction);
