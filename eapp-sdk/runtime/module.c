/* SPDX-License-Identifier: MIT */

/* Modules: "EMOD", the version, the image's build id, the bytes of text and
 * data, the bytes of bss and the relocation count as little-endian words;
 * then text and data linked at 0, the table first; then, in ascending order,
 * the offset of every word that holds an address inside the module. The
 * loader reads the module into one heap block, adds the block's address to
 * those words, clears bss and cleans the new code out of the data cache. */

#include "eapp.h"
#include "eapp_libc.h"
#include "platform.h"

#define MODULE_VERSION 1
#define MODULE_MAX_BYTES ((8u << 20) - 8) /* eapp_alloc's largest block */
/* A GNU build id note's head: name and descriptor sizes, type and "GNU";
 * the 16 bytes of --build-id=md5 follow. */
static const uint8_t note_head[16] = {4, 0, 0, 0, 16, 0, 0, 0, 3, 0, 0, 0, 'G', 'N', 'U', 0};

struct module_header {
    char magic[4];
    uint32_t version;
    uint8_t build_id[16];
    uint32_t image_bytes;
    uint32_t bss_bytes;
    uint32_t relocations;
};

/* The image's build id note, which eapp.lds places here. Without one the
 * symbol marks the startup code, which never reads as a note's head. */
extern const uint8_t __eapp_build_id[];

static const char *check(const struct module_header *header, uint32_t file_bytes)
{
    if (memcmp(header->magic, "EMOD", 4) || header->version != MODULE_VERSION)
        return "not a module";
    if (memcmp(__eapp_build_id, note_head, sizeof note_head))
        return "the image has no build id";
    if (memcmp(header->build_id, __eapp_build_id + sizeof note_head, sizeof header->build_id))
        return "built for another image";
    if (header->image_bytes < 4 || header->image_bytes % 4 ||
        header->image_bytes > MODULE_MAX_BYTES ||
        header->bss_bytes > MODULE_MAX_BYTES - header->image_bytes ||
        header->relocations > header->image_bytes / 4 ||
        file_bytes != sizeof *header + header->image_bytes + header->relocations * 4)
        return "damaged";
    return NULL;
}

/* Adds the base to each word the table names, reading the table in pieces. */
static const char *relocate(struct eapp_file *file, uint8_t *base,
                            const struct module_header *header)
{
    uint32_t offsets[64];
    uint32_t next = 0;
    for (uint32_t done = 0; done < header->relocations;) {
        uint32_t count = header->relocations - done;
        if (count > sizeof offsets / 4)
            count = sizeof offsets / 4;
        if (eapp_file_read(file, offsets, count * 4) != (int)(count * 4))
            return "read failed";
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t offset = offsets[i];
            if (offset < next || offset % 4 || offset > header->image_bytes - 4)
                return "damaged";
            uint32_t word;
            memcpy(&word, base + offset, 4);
            word += (uint32_t)(uintptr_t)base;
            memcpy(base + offset, &word, 4);
            next = offset + 4;
        }
        done += count;
    }
    return NULL;
}

const void *eapp_module_load(const char *path)
{
    struct eapp_file *file = eapp_file_open(path);
    if (!file) {
        eapp_logf("%s: cannot open", path);
        return NULL;
    }
    struct module_header header;
    const char *problem = NULL;
    uint8_t *base = NULL;
    if (eapp_file_read(file, &header, sizeof header) != (int)sizeof header)
        problem = "not a module";
    else
        problem = check(&header, eapp_file_size(file));
    if (!problem) {
        size_t bytes = (size_t)header.image_bytes + header.bss_bytes;
        base = eapp_alloc(bytes);
        if (!base) {
            eapp_logf("%s: no memory for %u bytes", path, (unsigned)bytes);
            eapp_file_close(file);
            return NULL;
        }
        if (eapp_file_read(file, base, header.image_bytes) != (int)header.image_bytes)
            problem = "read failed";
        else
            problem = relocate(file, base, &header);
    }
    eapp_file_close(file);
    if (problem) {
        eapp_free(base);
        eapp_logf("%s: %s", path, problem);
        return NULL;
    }
    memset(base + header.image_bytes, 0, header.bss_bytes);
    eapp_cache_sync_code(base, header.image_bytes);
    return base;
}

void eapp_module_free(const void *module)
{
    eapp_free((void *)module);
}
