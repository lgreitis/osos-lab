/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include "../../../payload/album_artists_menu.h"

int main(void)
{
    /* Every native choice, including Reset, must retain its original action. */
    for (unsigned variant = 0; variant < 4; variant++) {
        unsigned menu = variant & 1;
        uint32_t count = menu ? 14 : variant < 2 ? 34 : 35;
        uint32_t native = 0, custom = 0;
        for (uint32_t row = 0; row < count; row++) {
            uint32_t index = album_artists_menu_index(menu, row);
            if (index == UINT32_MAX) {
                assert(row == (menu ? 4u : 5u));
                custom++;
            } else {
                assert(index == native++);
            }
        }
        assert(custom == 1 && native == count - 1);
        assert(album_artists_menu_index(menu, count - 1) == count - 2);
    }
    for (uint32_t bits = 0; bits < 4; bits++) {
        struct album_artists_menu_file file = album_artists_menu_encode(bits);
        assert(album_artists_menu_valid(&file));
        assert(file.visible == bits);
        file.visible ^= 1;
        assert(!album_artists_menu_valid(&file));
        file = album_artists_menu_encode(bits);
        file.version++;
        assert(!album_artists_menu_valid(&file));
        file = album_artists_menu_encode(bits);
        file.magic = 0;
        assert(!album_artists_menu_valid(&file));
    }
    struct album_artists_menu_file invalid = album_artists_menu_encode(4);
    assert(!album_artists_menu_valid(&invalid));
    return 0;
}
