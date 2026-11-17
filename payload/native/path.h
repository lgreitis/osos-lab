/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

static inline void osos_path_init(struct osos_path *path, const char *name)
{
    unsigned i = 0;
    path->volume = 0;
    for (; i < sizeof(path->text) - 1 && name[i]; i++)
        path->text[i] = name[i];
    path->text[i] = 0;
}
