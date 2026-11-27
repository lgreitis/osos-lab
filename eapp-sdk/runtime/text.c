/* SPDX-License-Identifier: MIT */

#include "platform.h"

int eapp_text_finish(char *out, size_t capacity, size_t written, size_t length)
{
    if (capacity == 0)
        return length > 0x7fffffff ? 0x7fffffff : (int)length;
    if (written >= capacity)
        written = capacity - 1;
    if (length > written) {
        /* Cut back to the start of the character the cut went through. */
        while (written > 0 && ((unsigned char)out[written] & 0xc0) == 0x80)
            --written;
    }
    out[written] = 0;
    return length > 0x7fffffff ? 0x7fffffff : (int)length;
}

int eapp_text_copy(char *out, size_t capacity, const char *text)
{
    size_t length = 0;
    while (text[length])
        ++length;
    if (capacity == 0)
        return eapp_text_finish(out, 0, 0, length);
    size_t written = length < capacity - 1 ? length : capacity - 1;
    memcpy(out, text, written);
    out[written] = text[written];
    return eapp_text_finish(out, capacity, written, length);
}
