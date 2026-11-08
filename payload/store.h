/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

/* One record per feature. Keys are stored on disk; never reuse one. */
enum {
    CFW_STORE_CUSTOM_EQ = 0x20514543,       /* "CEQ " */
    CFW_STORE_DARK = 0x4b524144,            /* "DARK" */
    CFW_STORE_MENU_VISIBILITY = 0x5349564d, /* "MVIS" */
};

/* Settings live in RAM and reach the disk only through cfw_store_flush(), which
 * runs at boot after a migration, before sleep and around USB disk mode. */
void cfw_store_load(void);
/* Nonzero until the first successful write when no valid store file existed:
 * features then import their legacy files. */
int cfw_store_migrating(void);
/* Copies at most capacity bytes of the record and returns the bytes copied, or 0 if
 * absent: a record written by a newer build may be longer, so readers take its prefix. */
uint32_t cfw_store_get(uint32_t key, void *data, uint32_t capacity);
void cfw_store_set(uint32_t key, const void *data, uint32_t size);
/* Keeps the store dirty on failure so the next flush retries. */
void cfw_store_flush(void);

/* Legacy import: called once after the first successful write of a migrated store. */
void cfw_store_delete_legacy(void);
