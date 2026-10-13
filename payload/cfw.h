/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_H
#define CFW_H

static inline void cfw_copy_bytes(void *out, const void *in, unsigned size)
{
    unsigned char *destination = out;
    const unsigned char *source = in;
    for (unsigned i = 0; i < size; i++)
        destination[i] = source[i];
}

static inline void cfw_zero_bytes(void *out, unsigned size)
{
    unsigned char *destination = out;
    for (unsigned i = 0; i < size; i++)
        destination[i] = 0;
}

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
