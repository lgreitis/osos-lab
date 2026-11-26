/* SPDX-License-Identifier: GPL-3.0-only */
#include "screen.h"
#include "runtime.h"
#include "cfw.h"
#include "osos.h"
#include "album_artist.h"
#include "store.h"
#include "../../game-sdk/runtime/native_directory.h"

/* RVX format 1 and RVMF version 4, as emitted by Revola SDK 1.86. */
struct rvx_header {
    uint32_t magic, format, sdk_api, firmware, manifest_bytes, entries, hash;
};

struct module_header {
    u32 text, data, bss, relocs, entry, imports, exports;
};

struct symbol_import {
    u32 offset, library, symbol;
};

enum { MAX_MANIFEST = 1024, MAX_IMAGE = 2 * 1024 * 1024 };

Pkg *rvx_packages;

u32 rvx_hash(const char *s)
{
    u32 h = 2166136261u;
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static int order(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void collect(Pkg **list, const char *folder, const char *name)
{
    unsigned len = 0, prefix = 0;
    while (folder[prefix])
        prefix++;
    while (name && name[len]) {
        if (name[len] == '/' || name[len] == '\\' || name[len] == ':' ||
            prefix + 1 + len + 1 >= sizeof(((struct osos_path *)0)->text))
            return;
        len++;
    }
    if (len <= 4 || name[len - 4] != '.' || (name[len - 3] | 32) != 'r' ||
        (name[len - 2] | 32) != 'v' || (name[len - 1] | 32) != 'x')
        return;
    Pkg *file = osos_import_alloc(sizeof *file + prefix + len + 2);
    if (!file)
        return;
    for (unsigned i = 0; i < prefix; i++)
        file->path[i] = folder[i];
    file->path[prefix] = '/';
    for (unsigned i = 0; i <= len; i++)
        file->path[prefix + 1 + i] = name[i];
    while (*list && order((*list)->path, file->path) < 0)
        list = &(*list)->next;
    file->id[0] = 0;
    file->next = *list;
    *list = file;
}

static void scan(Pkg **list, const char *folder)
{
    struct osos_directory_iterator dir;
    struct osos_native_path_string path;
    uint8_t is_directory;
    osos_path_string_init(&path);
    osos_directory_init(&dir, folder, 0);
    while (osos_directory_next(&dir, &path, &is_directory)) {
        if (!is_directory)
            collect(list, folder, osos_path_string_text(&path));
    }
    osos_directory_destroy(&dir);
    osos_path_string_destroy(&path);
}

static int read_exact(void *handle, void *buffer, uint32_t bytes)
{
    uint32_t got = 0;
    return !osos_file_read(handle, buffer, bytes, &got) && got == bytes;
}

static int strings_valid(Pkg *p, unsigned off, unsigned count)
{
    while (count--) {
        if (off >= p->manifest->strings || !p->strings[off])
            return 0;
        while (p->strings[off])
            off++;
        off++;
    }
    return 1;
}

static int inspect(Pkg *p, void *f)
{
    struct rvx_header h;
    u32 bytes;
    if (!read_exact(f, &h, sizeof h) || h.magic != 0x30585652 || h.format != 1 ||
        h.firmware != RVX_FIRMWARE_ID || (h.sdk_api >> 16) != RVX_API_MAJOR ||
        h.sdk_api < RVX_API_LAYOUT || h.manifest_bytes < sizeof(struct rvx_manifest) ||
        h.manifest_bytes > MAX_MANIFEST || !h.entries || h.entries > 4096 ||
        osos_file_size(f, &bytes))
        return 0;
    u32 cat = (h.manifest_bytes + 3) & ~3u,
        metadata = cat + h.entries * sizeof(struct entry);
    if (bytes < sizeof h + metadata)
        return 0;
    uint8_t block[512];
    u32 hash = 2166136261u;
    for (u32 left = bytes - sizeof h; left;) {
        u32 n = left < sizeof block ? left : sizeof block;
        if (!read_exact(f, block, n))
            return 0;
        for (u32 i = 0; i < n; i++)
            hash = (hash ^ block[i]) * 16777619u;
        left -= n;
    }
    if (hash != h.hash || osos_file_seek(f, sizeof h, 0))
        return 0;
    struct rvx_manifest *m = p->manifest = osos_import_alloc(metadata);
    if (!m || !read_exact(f, m, metadata) || m->magic != 0x464d5652 ||
        m->version != 4 || (m->minimum_api >> 16) != RVX_API_MAJOR ||
        m->minimum_api > RVX_API || m->category < 1 || m->category > 5 || !m->modules ||
        m->modules > RVX_MODULES || m->modules > h.entries || m->sites > RVX_RAW_MAX ||
        m->content_count > 4 || m->library_count > 4 ||
        m->optional_count > m->library_count)
        return 0;
    p->sites = (const void *)(m + 1);
    p->records = (const void *)(p->sites + m->sites);
    p->strings = (const char *)(p->records + m->modules);
    unsigned table = p->strings - (const char *)m;
    if (table >= h.manifest_bytes || m->strings != h.manifest_bytes - table ||
        p->strings[m->strings - 1] || !strings_valid(p, m->text[0], 1) ||
        !strings_valid(p, m->content, m->content_count) ||
        !strings_valid(p, m->libraries, m->library_count))
        return 0;
    const char *id = p->strings + m->text[0];
    if (*id < 'a' || *id > 'z')
        return 0;
    for (unsigned i = 0; id[i]; i++) {
        char c = id[i];
        if (i >= sizeof p->id - 1 ||
            !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return 0;
        p->id[i] = c;
    }
    for (unsigned i = 0; i < m->sites; i++) {
        const struct rvx_site *raw = p->sites + i;
        if (!strings_valid(p, raw->name, 1) || (raw->address & 3) || raw->argc > 4 ||
            (raw->kind != 0 && raw->kind != 1 && raw->kind != 4) ||
            (raw->kind == 4 && raw->argc))
            return 0;
        for (unsigned j = 0; j < i; j++)
            if (p->sites[j].address == raw->address)
                return 0;
    }
    if (!strings_valid(p, p->records[0].name, 1) ||
        !cfw_string_equal(p->strings + p->records[0].name, "main"))
        return 0;
    p->entries = h.entries;
    p->catalog = (void *)((uint8_t *)m + cat);
    for (unsigned i = 0; i < h.entries; i++) {
        const struct entry *e = p->catalog + i;
        if (e->kind > 2 || e->reserved || e->codec > 1 || e->preload > 2 ||
            e->offset < sizeof h + metadata || (e->offset & 3) || e->offset > bytes ||
            e->stored > bytes - e->offset || (!e->codec && e->stored != e->size) ||
            (i < m->modules) != (e->kind == 0))
            return 0;
        if (i < m->modules) {
            const struct module_record *r = p->records + i;
            if (e->codec || e->size < sizeof(struct module_header) ||
                e->size > 2 * MAX_IMAGE || !strings_valid(p, r->name, 1) ||
                rvx_hash(p->strings + r->name) != e->name ||
                (!i) != (r->trigger == 0) || r->trigger > 2 || r->preload > 2 ||
                r->reserved || (r->trigger == 1) != !!r->screens ||
                !strings_valid(p, r->first_screen, r->screens))
                return 0;
        }
    }
    p->modules = osos_import_alloc(m->modules * sizeof *p->modules);
    return p->modules != NULL;
}

static Pkg *library(u32 hash)
{
    for (Pkg *p = rvx_packages; p; p = p->next)
        if (p->manifest->category == 5 && rvx_hash(p->id) == hash)
            return p;
    return NULL;
}

/* The library `reprise` comes from the core: RepriseOS services outside the
 * rv_api table. Extensions list it as optional, so they run on other cores. */
static const struct {
    const char *name;
    const void *function;
} reprise_exports[] = {
    {"reprise_legacy_preference", cfw_store_get},
    {"reprise_album_artist_read", cfw_album_artist_read},
};

static int reprise_export(u32 symbol, u32 *value)
{
    for (unsigned i = 0; i < sizeof reprise_exports / sizeof *reprise_exports; i++)
        if (rvx_hash(reprise_exports[i].name) == symbol) {
            *value = (u32)reprise_exports[i].function;
            return 1;
        }
    return 0;
}

/* Whether the manifest's library at `index` is optional: its imports stay 0
 * when it is missing, not running or lacks the function. */
static int optional(const Pkg *p, unsigned index)
{
    return index >= (unsigned)(p->manifest->library_count - p->manifest->optional_count);
}

static int resolve(Pkg *p, const struct symbol_import *imp, u32 *value)
{
    const char *s = p->strings + p->manifest->libraries;
    for (unsigned i = 0; i < p->manifest->library_count; i++) {
        if (rvx_hash(s) == imp->library) {
            if (imp->library == rvx_hash("reprise")) {
                if (reprise_export(imp->symbol, value))
                    return 1;
                *value = 0;
                return optional(p, i);
            }
            Pkg *lib = library(imp->library);
            if (lib && lib->state == 2) {
                const struct rvx_module *m = lib->modules;
                for (unsigned j = 0; j < m->export_count; j++)
                    if (m->exports[j].name == imp->symbol) {
                        *value = (u32)m->image + m->exports[j].offset;
                        return 1;
                    }
            }
            *value = 0;
            return optional(p, i);
        }
        while (*s++) {
        }
    }
    return 0;
}

static int within(const struct rvx_module *m, const void *ptr, u32 count, u32 stride)
{
    u32 offset = (uintptr_t)ptr - (uintptr_t)m->image;
    return !count ||
           (!(offset & 3) && offset < m->size && count <= (m->size - offset) / stride);
}

static int link_module(Pkg *p, u32 index, const struct module_header *h, File *file)
{
    struct rvx_module *m = p->modules + index;
    u32 bytes = p->catalog[index].size - sizeof *h;
    if ((h->text | h->data | h->bss) & 3 || h->text < 4 || h->text > MAX_IMAGE ||
        h->data > MAX_IMAGE - h->text || h->bss > MAX_IMAGE - h->text - h->data ||
        h->entry >= h->text || (h->entry & 3) == 2)
        return 0;
    u32 code = h->text + h->data;
    if (code > bytes || h->relocs > (bytes - code) / 4)
        return 0;
    bytes -= code + 4 * h->relocs;
    if (h->imports > bytes / sizeof(struct symbol_import))
        return 0;
    bytes -= h->imports * sizeof(struct symbol_import);
    if (bytes % 8 || h->exports != bytes / 8)
        return 0;
    m->size = code + h->bss;
    m->text = h->text;
    m->image = osos_import_alloc(m->size + bytes);
    if (!m->image)
        return 0;
    if (!rvx_read_exact(p, file, m->image, code))
        return 0;
    u32 offsets[32], previous = 0;
    for (u32 done = 0; done < h->relocs;) {
        u32 count = h->relocs - done;
        if (count > sizeof offsets / sizeof *offsets)
            count = sizeof offsets / sizeof *offsets;
        if (!rvx_read_exact(p, file, offsets, count * sizeof *offsets))
            return 0;
        for (u32 i = 0; i < count; i++) {
            u32 off = offsets[i];
            if ((off & 3) || off > code - 4 || ((done || i) && off <= previous))
                return 0;
            u32 *word = (void *)(m->image + off);
            if (*word > m->size)
                return 0;
            *word += (uintptr_t)m->image;
            previous = off;
        }
        done += count;
    }
    for (u32 i = 0; i < h->imports; i++) {
        struct symbol_import imp;
        if (!rvx_read_exact(p, file, &imp, sizeof imp) || (imp.offset & 3) ||
            imp.offset > code - 4 || !resolve(p, &imp, (u32 *)(m->image + imp.offset)))
            return 0;
    }
    m->exports = (void *)(m->image + m->size);
    m->export_count = h->exports;
    if (bytes && !rvx_read_exact(p, file, m->exports, bytes))
        return 0;
    for (u32 i = 0; i < h->exports; i++)
        if (m->exports[i].offset >= m->size)
            return 0;
    cfw_rvx_sync(m->image, m->image + code);
    typedef const struct rvx_descriptor *(*entry_fn)(const struct rvx_api *, Pkg *);
    const struct rvx_descriptor *d = ((entry_fn)(m->image + h->entry))(&cfw_rvx_api, p);
    if (!d)
        return index != 0 || p->manifest->category == 5;
    if (!within(m, d, 1, sizeof *d) || d->size != sizeof *d ||
        !within(m, d->hooks, d->count, sizeof *d->hooks) ||
        !within(m, d->settings, d->setting_count, sizeof *d->settings) ||
        !within(m, d->screens, d->screen_count, sizeof(struct screen)) ||
        !within(m, d->entries, d->entry_count, sizeof(struct menu_entry)) ||
        (index == 0 && !rvx_entries_valid(p, d->entries, d->entry_count)))
        return 0;
    m->desc = d;
    for (u32 i = 0; i < d->count; i++)
        if (rvx_bind(p, d->hooks + i) != OK)
            return 0;
    return 1;
}

static void release(Pkg *p, struct rvx_module *m)
{
    if (!m->image)
        return;
    m->loading = 1;
    if (m->desc && m->desc->unload)
        m->desc->unload();
    rvx_stop_timers(p, m);
    rvx_remove_hooks(p, m);
    rvx_forget_colors(p, m->image, m->size);
    rvx_forget_provider(p, m->image, m->size);
    osos_import_free(m->image);
    m->image = NULL;
    m->desc = NULL;
    m->exports = NULL;
    m->export_count = 0;
    m->size = m->text = 0;
    m->loading = m->pending_unload = 0;
}

int rvx_load_module(Pkg *p, u32 i)
{
    struct rvx_module *m = p->modules + i;
    if (m->loading)
        return CONFLICT;
    if (m->image)
        return OK;
    m->loading = 1;
    File *file;
    int ok = 0;
    if (rvx_open_entry(p, p->catalog + i, &file) == OK) {
        struct module_header header;
        ok = rvx_read_exact(p, file, &header, sizeof header) &&
             link_module(p, i, &header, file);
        rvx_file_close(p, file);
    }
    if (!ok)
        release(p, m);
    m->loading = 0;
    return ok ? OK : NOT_FOUND;
}

int rvx_load(Pkg *p, u32 i)
{
    if (i >= p->manifest->modules || p->records[i].trigger != 2)
        return BAD_ARGUMENT;
    p->modules[i].pending_unload = 0;
    return rvx_load_module(p, i);
}

void rvx_unload(Pkg *p, u32 i)
{
    if (i && i < p->manifest->modules && p->records[i].trigger == 2 &&
        p->records[i].preload != 0 && !p->modules[i].loading) {
        for (struct frame *f = rvx_top; f; f = f->parent) {
            if (f->ctx.self == p && f->module == i) {
                p->modules[i].pending_unload = 1;
                return;
            }
        }
        release(p, p->modules + i);
    }
}

const void *rvx_service(Pkg *p, u32 i)
{
    return i < p->manifest->modules && p->modules[i].desc ? p->modules[i].desc->service
                                                          : NULL;
}

/* The main module's service of a running extension; NULL while it unloads. */
const void *rvx_extension_service(const char *id)
{
    for (Pkg *p = rvx_packages; id && p; p = p->next)
        if (cfw_string_equal(p->id, id))
            return p->state == 2 && !p->modules[0].loading ? rvx_service(p, 0) : NULL;
    return NULL;
}

static int start(Pkg *p)
{
    if (p->state)
        return p->state == 2;
    p->state = 1;
    const char *folder = p->strings + p->manifest->content;
    for (unsigned j = 0; j < p->manifest->content_count; j++) {
        for (Pkg *other = rvx_packages; other; other = other->next) {
            if (other->state != 2)
                continue;
            const char *name = other->strings + other->manifest->content;
            for (unsigned n = 0; n < other->manifest->content_count; n++) {
                if (cfw_string_equal(folder, name))
                    goto failed;
                while (*name++) {
                }
            }
        }
        while (*folder++) {
        }
    }
    const char *s = p->strings + p->manifest->libraries;
    for (u32 i = 0; i < p->manifest->library_count; i++) {
        u32 hash = rvx_hash(s);
        Pkg *lib = library(hash);
        if (hash != rvx_hash("reprise") && (!lib || !start(lib)) && !optional(p, i))
            goto failed;
        while (*s++) {
        }
    }
    if (!rvx_preload_assets(p))
        goto failed;
    if (rvx_load_module(p, 0) != OK)
        goto failed;
    for (u32 i = 1; i < p->manifest->modules; i++)
        if (!p->records[i].preload && rvx_load_module(p, i) != OK)
            goto failed;
    p->state = 2;
    if (p->modules[0].desc && p->modules[0].desc->changed)
        p->modules[0].desc->changed(0xffff);
    return 1;
failed:
    for (u32 i = p->manifest->modules; i; i--)
        release(p, p->modules + i - 1);
    rvx_cleanup(p);
    p->state = 3;
    return 0;
}

void cfw_rvx_boot(void)
{
    static int scanned;
    if (scanned)
        return;
    scanned = 1;
    scan(&rvx_packages, "/Reprise/Extensions");
    scan(&rvx_packages, "/Revola/Extensions");
    for (Pkg **link = &rvx_packages; *link;) {
        Pkg *p = *link;
        struct osos_path path;
        osos_path_init(&path, p->path);
        void *f;
        int ok = 0;
        if (!osos_file_open(&path, 1, &f)) {
            ok = inspect(p, f);
            osos_file_close(f);
        }
        for (Pkg *prior = rvx_packages; ok && prior != p; prior = prior->next)
            if (cfw_string_equal(prior->id, p->id))
                ok = 0;
        if (ok)
            link = &p->next;
        else {
            *link = p->next;
            if (p->manifest)
                osos_import_free(p->manifest);
            if (p->modules)
                osos_import_free(p->modules);
            osos_import_free(p);
        }
    }
    cfw_rvx_has_canvas = rvx_packages != NULL;
    for (Pkg *p = rvx_packages; p; p = p->next)
        start(p);
    rvx_install_menus();
}

void rvx_release_screen(Pkg *p, u32 index)
{
    release(p, p->modules + index);
}
