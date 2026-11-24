/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>

int rvx_decode(const uint8_t *src, uint32_t stored, uint8_t *dst, uint32_t size)
{
    uint32_t in = 0, out = 0;
    while (out < size && in < stored) {
        unsigned mask = src[in++];
        for (unsigned bit = 1; bit <= 128 && out < size; bit <<= 1) {
            if (in == stored)
                return 0;
            unsigned x = src[in++];
            if (!(mask & bit)) {
                dst[out++] = x;
                continue;
            }
            if (in == stored)
                return 0;
            unsigned dist = ((x & 15) << 8) + src[in++] + 1, n = (x >> 4) + 3;
            if (n == 18) {
                unsigned v;
                do {
                    if (in == stored || n > size - out)
                        return 0;
                    v = src[in++];
                    n += v;
                } while (v == 255);
            }
            if (dist > out || n > size - out)
                return 0;
            while (n--) {
                dst[out] = dst[out - dist];
                out++;
            }
        }
    }
    return in == stored && out == size;
}
