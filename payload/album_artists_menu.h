/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_ALBUM_ARTISTS_MENU_H
#define CFW_ALBUM_ARTISTS_MENU_H
#include <stdint.h>

enum { ALBUM_ARTISTS_MAIN_MENU, ALBUM_ARTISTS_MUSIC_MENU };

static inline uint32_t album_artists_menu_index(unsigned menu, uint32_t index)
{
    uint32_t inserted = menu == ALBUM_ARTISTS_MAIN_MENU ? 5 : 4;
    return index == inserted ? UINT32_MAX : index - (index > inserted);
}

struct album_artists_menu_file {
    uint32_t magic, version, visible, check;
};

static inline struct album_artists_menu_file album_artists_menu_encode(uint32_t visible)
{
    return (struct album_artists_menu_file){0x554e4d43, 1, visible, ~visible};
}

static inline int album_artists_menu_valid(const struct album_artists_menu_file *file)
{
    return file->magic == 0x554e4d43 && file->version == 1 &&
           file->visible <= 3 && file->check == ~file->visible;
}
#endif
