/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_PLAYLIST_ADD_H
#define CFW_PLAYLIST_ADD_H
#include "osos.h"

/* Song hold menus: add Add to Playlist (and Remove from Playlist inside one). */
void cfw_playlist_add_menu(struct osos_context_menu *menu);
/* Song chosen from a hold menu (NULL clears it). */
void cfw_playlist_add_select(struct osos_media_item *item);
/* Cover Flow: the song selected last came from this album's back side. */
void cfw_playlist_add_from_coverflow(int album);
/* Song list inside a playlist: remove the item's song from that playlist. */
void cfw_playlist_remove(void *controller, struct osos_media_item *item);
/* Playlist chooser: returns 1 when action was handled or support-queried. */
int cfw_playlist_add_action(void *controller, const char *action, uint32_t argument);

/* A playlist is being listed: apply saved changes still waiting for it. */
void cfw_playlist_seen(void *list);

#endif
