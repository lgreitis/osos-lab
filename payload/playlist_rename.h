/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_PLAYLIST_RENAME_H
#define CFW_PLAYLIST_RENAME_H
#include <stdint.h>

/* Returns 1 when action was a rename action (handled or support-queried). */
int cfw_playlist_rename_action(void *controller, const char *action,
                               uint32_t argument);

/* Load saved playlist renames (called with the other CFW settings at boot). */
void cfw_playlist_names_load(void);

/* After each library load: playlists whose saved ID is gone match by name. */
void cfw_playlist_names_reconcile(int (*exists)(const uint32_t *id));

/* A playlist's own name before any rename here (length-prefixed UTF-16). */
void cfw_playlist_own_name(void *list, uint16_t *name, uint16_t limit);

#endif
