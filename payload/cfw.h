/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_H
#define CFW_H

/* The candidate may be null; expected must point to a string. */
static inline int cfw_string_equal(const char *candidate, const char *expected)
{
    if (!candidate)
        return 0;
    while (*candidate && *candidate == *expected) {
        candidate++;
        expected++;
    }
    return *candidate == *expected;
}

#endif
