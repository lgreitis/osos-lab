/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../../payload/rvx/compression.c"

int main(void)
{
    uint8_t out[280];
    const uint8_t overlap[] = {4, 'A', 'B', 0x30, 1};
    assert(rvx_decode(overlap, sizeof overlap, out, 8));
    assert(!memcmp(out, "ABABABAB", 8));
    assert(!rvx_decode(overlap, sizeof overlap - 1, out, 8));
    assert(!rvx_decode(overlap, sizeof overlap, out, 7));
    assert(!rvx_decode(overlap, sizeof overlap, out, 2));

    const uint8_t extended[] = {2, 'x', 0xf0, 0, 255, 6};
    assert(rvx_decode(extended, sizeof extended, out, sizeof out));
    for (unsigned i = 0; i < sizeof out; i++)
        assert(out[i] == 'x');

    const uint8_t before_start[] = {1, 0, 0};
    assert(!rvx_decode(before_start, sizeof before_start, out, 3));
    assert(rvx_decode(NULL, 0, NULL, 0));
    return 0;
}
