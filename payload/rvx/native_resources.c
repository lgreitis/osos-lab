/* SPDX-License-Identifier: GPL-3.0-only
 * Native UI resource construction adapted from RevolaOS, copyright 2026 The RevolaOS Authors.
 */
#include "native_ui.h"

uint32_t core_word_at(const void *data, uint32_t offset)
{
    const uint8_t *p = (const uint8_t *)data + offset;
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t core_blocks_count(const void *list)
{
    return core_word_at(list, 0);
}

int core_block_at(const void *list, uint32_t index, struct core_block *out)
{
    const uint8_t *p = (const uint8_t *)list + 4;
    if (index >= core_blocks_count(list))
        return 0;
    for (uint32_t i = 0; i <= index; i++) {
        uint32_t bytes = core_word_at(p, 0), kind = core_word_at(p, 4);
        if (i == index) {
            out->data = p + 8;
            out->bytes = bytes;
            out->kind = kind;
            return 1;
        }
        p += 8 + ((bytes + 3) & ~3u);
    }
    return 0;
}

const uint8_t *core_block_with_word(const void *list, uint32_t offset, uint32_t id,
                                    uint32_t *bytes)
{
    struct core_block block;
    for (uint32_t i = 0; i < core_blocks_count(list); i++) {
        if (!core_block_at(list, i, &block))
            break;
        if (block.bytes > offset + 3 && core_word_at(block.data, offset) == id) {
            *bytes = block.bytes;
            return block.data;
        }
    }
    return 0;
}

void core_write_bytes(struct core_writer *w, const void *data, uint32_t bytes)
{
    if (w->out)
        for (uint32_t i = 0; i < bytes; i++)
            w->out[w->at + i] = ((const uint8_t *)data)[i];
    w->at += bytes;
}

void core_write_copy(struct core_writer *w, const void *data, uint32_t bytes,
                     const uint32_t *words, uint32_t count)
{
    uint32_t start = w->at;
    core_write_bytes(w, data, bytes);
    for (uint32_t i = 0; i < count; i++)
        core_write_word_at(w, start + words[2 * i], words[2 * i + 1]);
}

void core_write_word(struct core_writer *w, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 24)};
    core_write_bytes(w, bytes, 4);
}

void core_write_word_at(struct core_writer *w, uint32_t at, uint32_t value)
{
    if (w->out)
        for (unsigned b = 0; b < 4; b++)
            w->out[at + b] = (uint8_t)(value >> (8 * b));
}

static uint32_t length(const char *text)
{
    uint32_t n = 0;
    while (text[n])
        n++;
    return n;
}

void core_write_string(struct core_writer *w, const char *text)
{
    uint32_t bytes = length(text);
    core_write_word(w, bytes);
    core_write_bytes(w, text, bytes);
}

uint32_t core_write_block_begin(struct core_writer *w, uint32_t kind)
{
    uint32_t begin = w->at;
    core_write_word(w, 0);
    core_write_word(w, kind);
    return begin;
}

void core_write_block_end(struct core_writer *w, uint32_t begin)
{
    core_write_word_at(w, begin, w->at - begin - 8);
    while (w->at % 4)
        core_write_bytes(w, "", 1);
}

void core_write_event(struct core_writer *w, const char *name, const char *handler,
                      const char *action, const char *const *arguments, uint32_t count)
{
    core_write_string(w, name);
    core_write_bytes(w, "1", 1);
    core_write_string(w, handler);
    core_write_word(w, action != 0);
    if (!action)
        return;
    core_write_string(w, action);
    core_write_word(w, count);
    for (uint32_t i = 0; i < count; i++)
        core_write_string(w, arguments[i]);
}

void core_set_source(uint32_t id, uint32_t property, uint32_t destination)
{
    uint32_t source[6] = {1,        12,         Resource_SORC, Service_Settings,
                          property, destination};
    native_set(Resource_SORC, id, source, sizeof source);
}

void core_set_source2(uint32_t id, uint32_t title, uint32_t second,
                      uint32_t destination)
{
    uint32_t source[11] = {2,
                           12,
                           Resource_SORC,
                           Service_Settings,
                           title,
                           SourceDestination_Title,
                           12,
                           Resource_SORC,
                           Service_Settings,
                           second,
                           destination};
    native_set(Resource_SORC, id, source, sizeof source);
}

char *core_put_text(char *out, const char *end, const char *value)
{
    while (*value && out < end - 1)
        *out++ = *value++;
    *out = 0;
    return out;
}

char *core_put_number(char *out, const char *end, uint32_t value)
{
    char digits[12];
    int n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n && out < end - 1)
        *out++ = digits[--n];
    *out = 0;
    return out;
}

void core_row_event_name(char *name, unsigned bytes, uint32_t item, const char *suffix)
{
    char *end = name + bytes;
    char *p = core_put_text(name, end, "list.pid.");
    p = core_put_number(p, end, item);
    core_put_text(p, end, suffix);
}

uint32_t core_resource_take(uint32_t count)
{
    static uint32_t next = 0x71000000;
    if (!count || count > 0x72000000 - next)
        return 0;
    uint32_t id = next;
    next += count;
    return id;
}

void core_screens_handler_name(char *out, uint32_t item)
{
    static const char hex[] = "0123456789abcdef";
    out[0] = 'R';
    out[1] = 'v';
    for (unsigned i = 0; i < 8; i++)
        out[2 + i] = hex[item >> (28 - 4 * i) & 15];
    out[10] = 0;
}
