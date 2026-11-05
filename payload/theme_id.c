/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_id.h"

static const char *const reserved[] = {"default", "dark", "classic"};

int cfw_theme_id_valid(const char *id)
{
    unsigned i = 0;
    for (; i < CFW_THEME_ID_BYTES && id[i]; i++) {
        unsigned char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              (i && (c == '_' || c == '-'))))
            return 0;
    }
    if (i == CFW_THEME_ID_BYTES)
        return 0;
    for (unsigned n = 0; n < sizeof(reserved) / sizeof(*reserved); n++) {
        unsigned at = 0;
        while (reserved[n][at] && id[at] == reserved[n][at])
            at++;
        if (!reserved[n][at] && !id[at])
            return 0;
    }
    return 1;
}

int cfw_theme_id_field_valid(const char id[CFW_THEME_ID_BYTES])
{
    if (!cfw_theme_id_valid(id))
        return 0;
    unsigned i = 0;
    while (id[i])
        i++;
    for (; i < CFW_THEME_ID_BYTES; i++)
        if (id[i])
            return 0;
    return 1;
}
