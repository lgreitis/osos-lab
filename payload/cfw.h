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

/* Byte loops: the freestanding ARM payload has no memcpy or memset. */
static inline void cfw_copy(void *destination, const void *source, unsigned size)
{
    unsigned char *p = destination;
    const unsigned char *q = source;
    while (size--)
        *p++ = *q++;
}

static inline void cfw_zero(void *destination, unsigned size)
{
    unsigned char *p = destination;
    while (size--)
        *p++ = 0;
}

#endif
