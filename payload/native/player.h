/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_NATIVE_PLAYER_H
#define CFW_NATIVE_PLAYER_H

static inline int osos_list_view_row(void *view)
{
    typedef int (*fn)(void *);
    return ((fn)osos_method(view, 0x168))(view);
}

#endif
