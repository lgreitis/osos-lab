/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

/* One record per feature. Keys are stored on disk; never reuse one. */
enum {
    CFW_STORE_PLAYBACK_LOG = 0x474f4c50,    /* "PLOG" */
    CFW_STORE_CROSSFEED = 0x44454658,       /* "XFED" */
    CFW_STORE_CUSTOM_EQ = 0x20514543,       /* "CEQ " */
    CFW_STORE_DARK = 0x4b524144,            /* "DARK" */
    CFW_STORE_MENU_VISIBILITY = 0x5349564d, /* "MVIS" */
    CFW_STORE_NIGHT_SHIFT = 0x5448474e,     /* "NGHT" */
};

/* Settings live in RAM and reach the disk only through cfw_store_flush(), which
 * runs at boot after a migration, before sleep and around USB disk mode. */
void cfw_store_load(void);
/* Nonzero until the first successful write when no valid store file existed:
 * features then import their legacy files. */
int cfw_store_migrating(void);
/* Like read_status; an I/O error defers migration writes until the next boot. */
int cfw_store_read_legacy(const char *name, void *record, uint32_t size);
/* Copies at most capacity bytes of the record and returns the bytes copied, or 0 if
 * absent: a record written by a newer build may be longer, so readers take its prefix. */
uint32_t cfw_store_get(uint32_t key, void *data, uint32_t capacity);
void cfw_store_set(uint32_t key, const void *data, uint32_t size);
/* Preserves the last valid file in .bak before writing. Failed writes stay dirty. */
void cfw_store_flush(void);

/* After a successful write, delete only files marked as successfully imported. */
void cfw_store_delete_legacy(void);
