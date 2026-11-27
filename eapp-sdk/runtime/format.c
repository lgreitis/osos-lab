/* SPDX-License-Identifier: MIT */

/* A small printf for a C library-free app: %d %i %u %x %X %c %s %%, with the
 * '-' and '0' flags, a width, a '.' precision for %s, and 'l' accepted and
 * ignored since long is 32 bits. */

#include <stdarg.h>

#include "platform.h"

struct writer {
    char *out;
    size_t capacity;
    size_t length; /* Of the whole text, written or not. */
};

/* The byte after the last one that fits is stored too, so the final cut can
 * see whether it went through a UTF-8 character. */
static void put(struct writer *w, char c)
{
    if (w->length < w->capacity)
        w->out[w->length] = c;
    ++w->length;
}

static void pad(struct writer *w, unsigned count, char c)
{
    while (count--)
        put(w, c);
}

/* `text` of `length` with `prefix` before it, padded to `width` with spaces
 * on the chosen side, or with zeros between the prefix and the text. */
static void field(struct writer *w, const char *prefix, const char *text, unsigned length,
                  unsigned width, int left, int zero)
{
    unsigned prefix_length = prefix[0] ? 1 : 0;
    unsigned total = prefix_length + length;
    unsigned fill = width > total ? width - total : 0;
    if (!left && !zero)
        pad(w, fill, ' ');
    if (prefix_length)
        put(w, prefix[0]);
    if (!left && zero)
        pad(w, fill, '0');
    for (unsigned i = 0; i < length; ++i)
        put(w, text[i]);
    if (left)
        pad(w, fill, ' ');
}

static unsigned digits(char *out, uint32_t value, unsigned base, int upper)
{
    const char *alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char reversed[32];
    unsigned n = 0;
    do {
        reversed[n++] = alphabet[value % base];
        value /= base;
    } while (value);
    for (unsigned i = 0; i < n; ++i)
        out[i] = reversed[n - 1 - i];
    return n;
}

int eapp_format_list(char *out, size_t capacity, const char *format, va_list args)
{
    struct writer w = {out, out ? capacity : 0, 0};
    if (!format)
        format = "";
    for (const char *f = format; *f; ++f) {
        if (*f != '%') {
            put(&w, *f);
            continue;
        }
        ++f;
        int left = 0, zero = 0, precision = -1;
        unsigned width = 0;
        for (;; ++f) {
            if (*f == '-')
                left = 1;
            else if (*f == '0')
                zero = 1;
            else
                break;
        }
        while (*f >= '0' && *f <= '9')
            width = width * 10 + (unsigned)(*f++ - '0');
        if (*f == '.') {
            precision = 0;
            for (++f; *f >= '0' && *f <= '9'; ++f)
                precision = precision * 10 + (*f - '0');
        }
        while (*f == 'l')
            ++f;
        char number[32];
        switch (*f) {
        case 'd':
        case 'i': {
            int32_t value = va_arg(args, int32_t);
            uint32_t magnitude = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
            field(&w, value < 0 ? "-" : "", number, digits(number, magnitude, 10, 0), width,
                  left, zero);
            break;
        }
        case 'u':
            field(&w, "", number, digits(number, va_arg(args, uint32_t), 10, 0), width, left,
                  zero);
            break;
        case 'x':
        case 'X':
            field(&w, "", number, digits(number, va_arg(args, uint32_t), 16, *f == 'X'), width,
                  left, zero);
            break;
        case 'c': {
            char c = (char)va_arg(args, int);
            field(&w, "", &c, 1, width, left, 0);
            break;
        }
        case 's': {
            const char *text = va_arg(args, const char *);
            if (!text)
                text = "(null)";
            unsigned length = 0;
            while (text[length] && (precision < 0 || (int)length < precision))
                ++length;
            field(&w, "", text, length, width, left, 0);
            break;
        }
        case '%':
            put(&w, '%');
            break;
        case 0:
            --f; /* A lone % at the end: the loop's ++f lands on the terminator. */
            break;
        default:
            put(&w, '%');
            put(&w, *f);
        }
    }
    size_t written = w.length < w.capacity ? w.length : (w.capacity ? w.capacity - 1 : 0);
    if (w.capacity)
        return eapp_text_finish(out, w.capacity, written, w.length);
    return w.length > 0x7fffffff ? 0x7fffffff : (int)w.length;
}

int eapp_format(char *out, size_t capacity, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = eapp_format_list(out, capacity, format, args);
    va_end(args);
    return length;
}

void eapp_logf(const char *format, ...)
{
    char text[EAPP_LOG_SIZE];
    va_list args;
    va_start(args, format);
    eapp_format_list(text, sizeof text, format, args);
    va_end(args);
    eapp_log(text);
}
