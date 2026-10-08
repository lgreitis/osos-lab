/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_ALBUM_ARTIST_H
#define CFW_ALBUM_ARTIST_H
#include "osos.h"

/* Tokens belong to the native Artist/Composer cache and expire on library reload.
 * Zero means absent. Tokens identify cached text, not collation-equivalent names. */
uint32_t cfw_album_artist_token(struct osos_media_record *record);
uint32_t cfw_album_artist_read(struct osos_media_record *record,
                                uint16_t *text, uint32_t max_units);
#endif
