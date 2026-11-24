/* SPDX-License-Identifier: GPL-3.0-only */

/* Built with -fno-tree-loop-distribute-patterns: GCC would otherwise turn the
 * loops of memset and memcpy into calls to themselves. */

#include "rv_libc.h"

typedef uint32_t __attribute__((may_alias)) word;

void *memcpy(void *to, const void *from, size_t bytes)
{
    uint8_t *d = to;
    const uint8_t *s = from;
    if ((((uintptr_t)d | (uintptr_t)s) & 3) == 0) {
        for (; bytes >= 4; bytes -= 4, d += 4, s += 4)
            *(word *)d = *(const word *)s;
    }
    while (bytes--)
        *d++ = *s++;
    return to;
}

void *memmove(void *to, const void *from, size_t bytes)
{
    uint8_t *d = to;
    const uint8_t *s = from;
    if (d <= s || d >= s + bytes)
        return memcpy(to, from, bytes);
    while (bytes--)
        d[bytes] = s[bytes];
    return to;
}

void *memset(void *to, int value, size_t bytes)
{
    uint8_t *d = to;
    if (((uintptr_t)d & 3) == 0) {
        word fill = (uint8_t)value * 0x01010101u;
        for (; bytes >= 4; bytes -= 4, d += 4)
            *(word *)d = fill;
    }
    while (bytes--)
        *d++ = (uint8_t)value;
    return to;
}

int memcmp(const void *a, const void *b, size_t bytes)
{
    const uint8_t *x = a, *y = b;
    for (; bytes; bytes--, x++, y++) {
        if (*x != *y)
            return *x - *y;
    }
    return 0;
}

size_t strlen(const char *text)
{
    const char *end = text;
    while (*end)
        end++;
    return (size_t)(end - text);
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (uint8_t)*a - (uint8_t)*b;
}

struct writer {
    char *at, *end; /* end is the byte kept for the NUL */
};

static void put(struct writer *w, char c)
{
    if (w->at < w->end)
        *w->at++ = c;
}

static void pad(struct writer *w, char c, int count)
{
    while (count-- > 0)
        put(w, c);
}

uint32_t rv_vformat(char *out, uint32_t capacity, const char *format, va_list args)
{
    if (!capacity)
        return 0;
    struct writer w = {out, out + capacity - 1};
    while (*format) {
        if (*format != '%') {
            put(&w, *format++);
            continue;
        }
        const char *start = format++;
        int left = 0, zero = 0, width = 0, precision = -1;
        for (;; format++) {
            if (*format == '-')
                left = 1;
            else if (*format == '0')
                zero = 1;
            else
                break;
        }
        if (*format == '*') {
            width = va_arg(args, int);
            if (width < 0)
                left = 1, width = -width;
            format++;
        }
        while (*format >= '0' && *format <= '9')
            width = width * 10 + *format++ - '0';
        if (*format == '.') {
            format++;
            precision = 0;
            if (*format == '*') {
                precision = va_arg(args, int);
                format++;
            }
            while (*format >= '0' && *format <= '9')
                precision = precision * 10 + *format++ - '0';
        }
        if (*format == 'l')
            format++;

        char digits[12];
        const char *text = digits;
        int length, negative = 0;
        uint32_t value, base = 10;
        const char *alphabet = "0123456789abcdef";
        switch (*format) {
        case 'd':
        case 'i': {
            int32_t n = va_arg(args, int32_t);
            negative = n < 0;
            value = negative ? 0u - (uint32_t)n : (uint32_t)n;
            goto number;
        }
        case 'X':
            alphabet = "0123456789ABCDEF";
            /* fall through */
        case 'x':
            base = 16;
            /* fall through */
        case 'u':
            value = va_arg(args, uint32_t);
        number: {
            char *p = digits + sizeof digits;
            do
                *--p = alphabet[value % base];
            while ((value /= base));
            text = p;
            length = (int)(digits + sizeof digits - p);
            break;
        }
        case 'c':
            digits[0] = (char)va_arg(args, int);
            length = 1;
            break;
        case 's':
            text = va_arg(args, const char *);
            if (!text)
                text = "(null)";
            for (length = 0; text[length] && (precision < 0 || length < precision);
                 length++)
                ;
            zero = 0;
            break;
        case '%':
            put(&w, '%');
            format++;
            continue;
        default: /* not a conversion: copy it as written */
            while (start < format)
                put(&w, *start++);
            continue;
        }
        format++;

        int fill = width - length - negative;
        if (left)
            zero = 0;
        if (!left && !zero)
            pad(&w, ' ', fill);
        if (negative)
            put(&w, '-');
        if (zero)
            pad(&w, '0', fill);
        for (int i = 0; i < length; i++)
            put(&w, text[i]);
        if (left)
            pad(&w, ' ', fill);
    }
    *w.at = 0;
    return (uint32_t)(w.at - out);
}

uint32_t rv_format(char *out, uint32_t capacity, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    uint32_t length = rv_vformat(out, capacity, format, args);
    va_end(args);
    return length;
}

uint32_t rv_format_duration(char *out, uint32_t capacity, uint32_t seconds)
{
    unsigned int minutes = seconds / 60, rest = seconds % 60;
    if (minutes < 60)
        return rv_format(out, capacity, "%u:%02u", minutes, rest);
    return rv_format(out, capacity, "%u:%02u:%02u", minutes / 60, minutes % 60, rest);
}

static void put_decimal(struct writer *w, uint32_t value)
{
    char digits[10];
    unsigned int n = 0;
    do
        digits[n++] = (char)('0' + value % 10);
    while ((value /= 10));
    while (n)
        put(w, digits[--n]);
}

static void put_text(struct writer *w, const char *text)
{
    while (*text)
        put(w, *text++);
}

uint32_t rv_format_int(char *out, uint32_t capacity, int value, const char *unit)
{
    if (!capacity)
        return 0;
    struct writer w = {out, out + capacity - 1};
    if (value < 0)
        put(&w, '-');
    put_decimal(&w, value < 0 ? 0u - (uint32_t)value : (uint32_t)value);
    put_text(&w, unit);
    *w.at = 0;
    return (uint32_t)(w.at - out);
}

uint32_t rv_format_tenths(char *out, uint32_t capacity, int tenths, int plus,
                          const char *unit)
{
    if (!capacity)
        return 0;
    struct writer w = {out, out + capacity - 1};
    uint32_t magnitude = tenths < 0 ? 0u - (uint32_t)tenths : (uint32_t)tenths;
    if (tenths < 0)
        put(&w, '-');
    else if (plus && tenths)
        put(&w, '+');
    put_decimal(&w, magnitude / 10);
    put(&w, '.');
    put(&w, (char)('0' + magnitude % 10));
    put_text(&w, unit);
    *w.at = 0;
    return (uint32_t)(w.at - out);
}

/* Adapted from RepriseOS payload/eq_canvas.c canvas_frequency (93f6a45) by lgreitis. */
uint32_t rv_format_hz(char *out, uint32_t capacity, uint32_t hz)
{
    if (!capacity)
        return 0;
    struct writer w = {out, out + capacity - 1};
    uint32_t fraction = hz % 1000;
    put_decimal(&w, hz < 1000 ? hz : hz / 1000);
    if (hz >= 1000) {
        /* The three decimals of the kilohertz, trailing zeros dropped. */
        if (fraction)
            put(&w, '.');
        for (uint32_t place = 100; fraction; place /= 10) {
            put(&w, (char)('0' + fraction / place));
            fraction %= place;
        }
        put(&w, 'k');
    }
    put_text(&w, " Hz");
    *w.at = 0;
    return (uint32_t)(w.at - out);
}

uint32_t rv_copy_utf8(char *out, uint32_t capacity, const char *text)
{
    uint32_t i = 0;
    if (!capacity)
        return 0;
    for (; text && text[i] && i + 1 < capacity; i++)
        out[i] = text[i];
    /* A byte that continues a sequence is cut when its start was. */
    if (text && text[i])
        while (i && ((uint8_t)text[i] & 0xc0) == 0x80)
            i--;
    out[i] = 0;
    return i;
}
