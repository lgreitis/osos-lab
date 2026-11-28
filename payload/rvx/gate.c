/* SPDX-License-Identifier: GPL-3.0-only */
#include "host.h"
#include "native/layout.h"
#include "osos.h"
#include "patch.h"

extern const unsigned char __payload_limit[], __payload_start[];
extern void cfw_rvx_sync(const void *, const void *);
static const struct rvx_host *host;

uint32_t rvx_insert_slot;
void *rvx_chains[RVX_HOOK_SLOTS];

static int read_and_hash(void *f, void *dest, uint32_t n, uint32_t *hash)
{
    uint32_t got;
    if (osos_file_read(f, dest, n, &got) || got != n)
        return 0;
    const uint8_t *b = dest;
    for (unsigned i = 0; i < n; i++)
        *hash = (*hash ^ b[i]) * 16777619u;
    return 1;
}

static struct rvx_host *read_runtime(void *f)
{
    const struct rvx_trailer_spec *s = &rvx_trailer;
    uint32_t offset = CFW_PAYLOAD_FILE_OFFSET + (uintptr_t)__payload_limit -
                      (uintptr_t)__payload_start;
    if (osos_file_seek(f, offset, 0))
        return NULL;
    void *allocation = osos_import_alloc(s->memory + 7);
    if (!allocation)
        return NULL;
    uint8_t *image = (void *)(((uintptr_t)allocation + 7) & ~7u);
    uint32_t hash = 2166136261u;
    if (!read_and_hash(f, image, s->stored, &hash))
        goto failed;
    unsigned previous = 0;
    for (unsigned done = 0; done < s->relocations;) {
        uint16_t offsets[64];
        unsigned n = s->relocations - done;
        if (n > sizeof offsets / sizeof *offsets)
            n = sizeof offsets / sizeof *offsets;
        if (!read_and_hash(f, offsets, n * sizeof *offsets, &hash))
            goto failed;
        for (unsigned i = 0; i < n; i++) {
            unsigned off = offsets[i] * 4u;
            if (off > s->stored - 4 || ((i || done) && off <= previous))
                goto failed;
            uint32_t *word = (void *)(image + off);
            if (*word > s->memory)
                goto failed;
            *word += (uintptr_t)image;
            previous = off;
        }
        done += n;
    }
    if (hash != s->hash)
        goto failed;
    for (unsigned i = s->stored; i < s->memory; i++)
        image[i] = 0;
    cfw_rvx_sync(image, image + s->stored);
    return (void *)image;
failed:
    osos_import_free(allocation);
    return NULL;
}

static void *resource_bank;

void cfw_rvx_boot(void)
{
    static int started;
    if (started)
        return;
    started = 1;
    struct osos_path path;
    osos_path_init(&path, "/Reprise/Firmware/reprise.bin");
    void *file;
    if (osos_file_open(&path, 1, &file))
        return;
    host = read_runtime(file);
    osos_file_close(file);
    if (host) {
        if (resource_bank)
            host->register_ui(resource_bank);
        host->boot();
    }
}

void cfw_rvx_register_ui(void *bank)
{
    resource_bank = bank;
    if (host)
        host->register_ui(bank);
}

void cfw_rvx_flush(void)
{
    if (host)
        host->flush();
}

void cfw_rvx_reset(void)
{
    if (host)
        host->reset();
}

void cfw_rvx_menu_row(void *list, uint32_t table, uint32_t item, const void *row,
                      uint32_t bytes, uint32_t metadata)
{
    if (host)
        host->row(list, table, item, row, bytes, metadata);
}

void cfw_rvx_menu_events(void *map, uint32_t screen, uint32_t layout)
{
    if (host)
        host->events(map, screen, layout);
}

unsigned cfw_rvx_video_menu(void *menu, void *controller, int row, const void *item,
                            int tv)
{
    return host ? host->video(menu, controller, row, item, tv) : 0;
}

int cfw_rvx_indexed_property(uint32_t type, uint32_t property, uint32_t index,
                             uintptr_t *out)
{
    return host && host->indexed_property(type, property, index, out);
}

int cfw_rvx_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    return host && host->property(type, property, out);
}

unsigned cfw_rvx_visible_rows(unsigned menu)
{
    return host ? host->visible(menu) : 0;
}

int cfw_rvx_visible_property(unsigned menu, unsigned row, uint32_t type, int selected,
                             uintptr_t *out)
{
    return host && host->visibility(menu, row, type, selected, out);
}

void cfw_rvx_toggle_row(unsigned menu, unsigned row)
{
    if (host)
        host->toggle(menu, row);
}

uint32_t cfw_rvx_stock_view(uint32_t view)
{
    return host ? host->stock_view(view) : view;
}

uint32_t rvx_dispatch(uint32_t *registers, unsigned slot)
{
    return host->dispatch(registers, slot);
}
