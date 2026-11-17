/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_ALBUM_ARTISTS_H
#define CFW_ALBUM_ARTISTS_H
#include "osos.h"
void cfw_album_artists_invalidate(void *track_data);
int cfw_album_artists_owns_model(void *model);
int cfw_album_artists_release_artwork(void);
#endif
