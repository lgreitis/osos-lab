/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_input.h"

static int loading, latched;

int cfw_theme_input_begin(void)
{
    if (latched)
        return 0;
    loading = latched = 1;
    return 1;
}

void cfw_theme_input_finish(void)
{
    loading = 0;
}

void cfw_theme_input_drained(void)
{
    if (!loading)
        latched = 0;
}
