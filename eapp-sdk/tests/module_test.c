/* SPDX-License-Identifier: GPL-3.0-only */

/* The module loader against an in-memory package file: relocation, bss,
 * the cache range it syncs, and every refusal freeing what it took. A host
 * pointer has 64 bits, so a relocated word holds the low 32 bits of the
 * block's address, as on the iPod. */

#include "platform.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The image's build id note: sizes, type, "GNU" and the 16-byte id. */
const uint8_t __eapp_build_id[32] = {4, 0, 0, 0, 16, 0, 0, 0, 3, 0, 0, 0, 'G', 'N', 'U', 0,
                                     1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

static uint8_t file_data[4096];
static uint32_t file_bytes;
static int file_open, files_opened;
static unsigned live_blocks, refuse_alloc;
static const void *synced;
static size_t synced_bytes;
static char last_log[EAPP_LOG_SIZE];

struct eapp_file {
    uint32_t position;
};
static struct eapp_file the_file;

struct eapp_file *eapp_file_open(const char *path)
{
    if (strcmp(path, "a.emod"))
        return NULL;
    assert(!file_open);
    file_open = 1;
    ++files_opened;
    the_file.position = 0;
    return &the_file;
}

int eapp_file_read(struct eapp_file *file, void *buffer, size_t size)
{
    assert(file == &the_file && file_open);
    if (size > file_bytes - file->position)
        size = file_bytes - file->position;
    memcpy(buffer, file_data + file->position, size);
    file->position += size;
    return (int)size;
}

uint32_t eapp_file_size(const struct eapp_file *file)
{
    assert(file == &the_file && file_open);
    return file_bytes;
}

void eapp_file_close(struct eapp_file *file)
{
    assert(file == &the_file && file_open);
    file_open = 0;
}

void *eapp_alloc(size_t size)
{
    if (refuse_alloc)
        return NULL;
    ++live_blocks;
    void *block = malloc(size);
    memset(block, 0xa5, size); /* Not zero, so a missed bss clear shows. */
    return block;
}

void eapp_free(void *pointer)
{
    if (pointer)
        --live_blocks;
    free(pointer);
}

void eapp_logf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(last_log, sizeof last_log, format, args);
    va_end(args);
}

void eapp_cache_sync_code(const void *start, size_t bytes)
{
    synced = start;
    synced_bytes = bytes;
}

static void put32(uint32_t offset, uint32_t value)
{
    memcpy(file_data + offset, &value, 4);
}

/* A module of 16 bytes of text and data and 8 of bss; words 1 and 3 hold
 * addresses inside it (4 and 12 as linked), words 0 and 2 do not. */
static void make_module(void)
{
    memset(file_data, 0, sizeof file_data);
    memcpy(file_data, "EMOD", 4);
    put32(4, 1);
    memcpy(file_data + 8, __eapp_build_id + 16, 16);
    put32(24, 16);
    put32(28, 8);
    put32(32, 2);
    put32(36, 0x11111111);
    put32(40, 4);
    put32(44, 0x18000123);
    put32(48, 12);
    put32(52, 4);
    put32(56, 12);
    file_bytes = 60;
}

static uint32_t word(const void *module, unsigned index)
{
    uint32_t value;
    memcpy(&value, (const uint8_t *)module + index * 4, 4);
    return value;
}

static void refused(const char *log)
{
    last_log[0] = 0;
    assert(!eapp_module_load("a.emod"));
    assert(!file_open && live_blocks == 0);
    assert(strstr(last_log, log));
    make_module();
    refuse_alloc = 0;
}

int main(void)
{
    make_module();
    const void *module = eapp_module_load("a.emod");
    assert(module && !file_open && live_blocks == 1);
    uint32_t base = (uint32_t)(uintptr_t)module;
    assert(word(module, 0) == 0x11111111 && word(module, 1) == 4 + base);
    assert(word(module, 2) == 0x18000123 && word(module, 3) == 12 + base);
    assert(word(module, 4) == 0 && word(module, 5) == 0);
    assert(synced == module && synced_bytes == 16);
    eapp_module_free(module);
    assert(live_blocks == 0);
    eapp_module_free(NULL);

    assert(!eapp_module_load("missing.emod") && strstr(last_log, "cannot open"));
    memcpy(file_data, "EPAK", 4);
    refused("not a module");
    put32(4, 2);
    refused("not a module");
    file_data[8] ^= 1;
    refused("built for another image");
    put32(24, 18);
    refused("damaged");
    put32(28, 9u << 20);
    refused("damaged");
    put32(32, 5);
    refused("damaged");
    file_bytes = 56;
    refused("damaged");
    put32(56, 14);
    refused("damaged");
    put32(56, 16);
    refused("damaged");
    put32(56, 4);
    refused("damaged");
    file_bytes = 20;
    refused("not a module");
    refuse_alloc = 1;
    refused("no memory for 24 bytes");
    printf("module_test passed\n");
    return 0;
}
