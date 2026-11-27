/* SPDX-License-Identifier: GPL-3.0-only */
#include "screen.h"
#include "cfw.h"
#include "osos.h"
#include "../../game-sdk/runtime/native_directory.h"
#include "native/layout.h"
#include "runtime.h"

struct rvx_file {
    File *next;
    void *handle;
    u32 base, size, pos;
    int writable, bounded;
};

static int append(char *out, unsigned *used, const char *s)
{
    while (*s) {
        if (*used >= 257)
            return 0;
        out[(*used)++] = *s++;
    }
    out[*used] = 0;
    return 1;
}

static int build_path(Pkg *p, const char *name, int content, int folder,
                      struct osos_path *path)
{
    if (!name || (!*name && (!folder || content)))
        return 0;
    const char *part = name;
    for (const char *s = name; *name; s++) {
        if (*s == '\\' || *s == ':' || ((uint8_t)*s < 32 && *s))
            return 0;
        if (!*s || *s == '/') {
            unsigned n = s - part;
            /* A trailing "/" ends the name like its end does. */
            if (!*s && !n && part != name)
                break;
            if (!n || (part[0] == '.' && (n == 1 || (n == 2 && part[1] == '.'))))
                return 0;
            if (!*s)
                break;
            part = s + 1;
        }
    }
    if (content) {
        const char *s = p->strings + p->manifest->content;
        unsigned i;
        for (i = 0; i < p->manifest->content_count; i++) {
            /* FAT returns names in either case. */
            unsigned j = 0;
            while (s[j] && (s[j] | 32) == (name[j] | 32))
                j++;
            if (!s[j] && (!name[j] || name[j] == '/'))
                break;
            while (*s++) {
            }
        }
        if (i == p->manifest->content_count)
            return 0;
    }
    path->volume = 0;
    unsigned len = 0;
    return append(path->text, &len,
                  content ? "/Reprise/" : "/Reprise/Data/Extensions/") &&
           (content || (append(path->text, &len, p->id) &&
                        (!*name || append(path->text, &len, "/")))) &&
           append(path->text, &len, name);
}

static int open_path(Pkg *p, const char *name, int mode, File **out)
{
    *out = NULL;
    struct osos_path path;
    osos_path_init(&path, name);
    if (mode == 2) {
        char *last = NULL;
        for (char *s = path.text; *s; s++)
            if (*s == '/')
                last = s;
        if (last) {
            *last = 0;
            typedef int (*mkdir_fn)(const char *, u32);
            ((mkdir_fn)0x08090840)(path.text, 0);
            *last = '/';
        }
    }
    void *handle;
    if (osos_file_open(&path, mode, &handle))
        return NOT_FOUND;
    File *f = osos_import_alloc(sizeof *f);
    if (!f) {
        osos_file_close(handle);
        return NO_MEMORY;
    }
    f->handle = handle;
    f->writable = mode == 2;
    f->next = p->files;
    p->files = f;
    *out = f;
    return OK;
}

static File **find(Pkg *p, File *f)
{
    File **it = &p->files;
    while (*it && *it != f)
        it = &(*it)->next;
    return it;
}

int rvx_file_open(Pkg *p, const char *name, int mode, File **out)
{
    struct osos_path path;
    if (!out || (mode != 1 && mode != 2) || !build_path(p, name, 0, 0, &path))
        return BAD_ARGUMENT;
    return open_path(p, path.text, mode, out);
}

int rvx_content_open(Pkg *p, const char *name, File **out)
{
    struct osos_path path;
    if (!out || !build_path(p, name, 1, 0, &path))
        return BAD_ARGUMENT;
    return open_path(p, path.text, 1, out);
}

int rvx_firmware_open(Pkg *p, File **out)
{
    return out ? open_path(p, "/Reprise/Firmware/reprise.bin", 1, out) : BAD_ARGUMENT;
}

int rvx_file_read(Pkg *p, File *f, void *out, u32 bytes, u32 *got)
{
    if (!got)
        return BAD_ARGUMENT;
    *got = 0;
    if (!f || !*find(p, f) || (!out && bytes))
        return BAD_ARGUMENT;
    if (f->bounded && bytes > f->size - f->pos)
        bytes = f->size - f->pos;
    int result = bytes ? osos_file_read(f->handle, out, bytes, got) : 0;
    if (result && result != -39)
        return NOT_FOUND;
    f->pos += *got;
    return OK;
}

int rvx_file_write(Pkg *p, File *f, const void *data, u32 bytes)
{
    u32 n;
    if (!f || !*find(p, f) || !f->writable || (!data && bytes))
        return BAD_ARGUMENT;
    return !osos_file_write(f->handle, data, bytes, &n) && n == bytes ? OK : NOT_FOUND;
}

int rvx_file_seek(Pkg *p, File *f, u32 offset)
{
    if (!f || !*find(p, f) || (f->bounded && offset > f->size))
        return BAD_ARGUMENT;
    if (osos_file_seek(f->handle, (uint64_t)f->base + offset, 0))
        return NOT_FOUND;
    f->pos = offset;
    return OK;
}

void rvx_file_close(Pkg *p, File *f)
{
    if (!f)
        return;
    File **it = find(p, f);
    if (!*it)
        return;
    *it = f->next;
    osos_file_close(f->handle);
    osos_import_free(f);
}

int rvx_file_delete(Pkg *p, const char *name)
{
    struct osos_path path;
    if (!build_path(p, name, 0, 0, &path))
        return BAD_ARGUMENT;
    return osos_file_delete(&path) ? NOT_FOUND : OK;
}

static int list(Pkg *p, const char *name, int content, ListFn each, void *arg)
{
    struct osos_path path;
    if (!each || !build_path(p, name, content, 1, &path))
        return BAD_ARGUMENT;
    struct osos_directory_iterator dir;
    struct osos_native_path_string entry;
    uint8_t is_dir;
    osos_path_string_init(&entry);
    osos_directory_init(&dir, path.text, 0);
    while (osos_directory_next(&dir, &entry, &is_dir))
        each(osos_path_string_text(&entry), is_dir, arg);
    osos_directory_destroy(&dir);
    osos_path_string_destroy(&entry);
    return OK;
}

int rvx_folder_list(Pkg *p, const char *name, ListFn each, void *arg)
{
    return list(p, name, 0, each, arg);
}

int rvx_content_list(Pkg *p, const char *name, ListFn each, void *arg)
{
    return list(p, name, 1, each, arg);
}

static const struct entry *asset(Pkg *p, const char *name)
{
    if (name) {
        u32 hash = rvx_hash(name);
        for (u32 i = p->manifest->modules; i < p->entries; i++)
            if (p->catalog[i].name == hash)
                return p->catalog + i;
    }
    return NULL;
}

int rvx_open_entry(Pkg *p, const struct entry *e, File **out)
{
    int status = open_path(p, p->path, 1, out);
    if (status != OK)
        return status;
    File *f = *out;
    f->bounded = 1;
    f->base = e->offset;
    f->size = e->stored;
    if (rvx_file_seek(p, f, 0) == OK)
        return OK;
    rvx_file_close(p, f);
    *out = NULL;
    return NOT_FOUND;
}

int rvx_read_exact(Pkg *p, File *f, void *out, u32 n)
{
    u32 got = 0;
    return rvx_file_read(p, f, out, n, &got) == OK && got == n;
}

static int read_asset(Pkg *p, const struct entry *e, void *out)
{
    File *f;
    if (rvx_open_entry(p, e, &f) != OK)
        return 0;
    int ok = rvx_read_exact(p, f, out, e->stored);
    rvx_file_close(p, f);
    return ok;
}

int rvx_asset_open(Pkg *p, const char *name, File **out)
{
    const struct entry *e = asset(p, name);
    if (!out)
        return BAD_ARGUMENT;
    *out = NULL;
    if (!e)
        return NOT_FOUND;
    if (e->codec || e->preload != 2)
        return BAD_ARGUMENT;
    return rvx_open_entry(p, e, out);
}

int rvx_asset_read(Pkg *p, const char *name, u32 offset, void *out, u32 n, u32 *got)
{
    const struct entry *e = asset(p, name);
    if (!got)
        return BAD_ARGUMENT;
    *got = 0;
    if (!e)
        return NOT_FOUND;
    if (e->codec || (n && !out))
        return BAD_ARGUMENT;
    if (offset >= e->size)
        return OK;
    const uint8_t *cached = p->preloaded ? p->preloaded[e - p->catalog] : NULL;
    if (cached) {
        if (n > e->size - offset)
            n = e->size - offset;
        for (u32 i = 0; i < n; i++)
            ((uint8_t *)out)[i] = cached[offset + i];
        *got = n;
        return OK;
    }
    File *f;
    int status = rvx_open_entry(p, e, &f);
    if (status != OK)
        return status;
    if ((status = rvx_file_seek(p, f, offset)) == OK)
        status = rvx_file_read(p, f, out, n, got);
    rvx_file_close(p, f);
    return status;
}

void *rvx_asset_load(Pkg *p, const char *name, u32 *bytes)
{
    const struct entry *e = asset(p, name);
    if (bytes)
        *bytes = 0;
    if (!e || e->size > 16 * 1024 * 1024)
        return NULL;
    uint8_t *cached = p->preloaded ? p->preloaded[e - p->catalog] : NULL;
    uint8_t *out = rvx_alloc(p, e->size ? e->size : 1);
    if (!out)
        return NULL;
    int ok = 1;
    if (!e->codec) {
        if (cached)
            for (u32 i = 0; i < e->size; i++)
                out[i] = cached[i];
        else
            ok = read_asset(p, e, out);
    } else {
        uint8_t *src = cached ? cached : osos_import_alloc(e->stored ? e->stored : 1);
        ok = src && (cached || read_asset(p, e, src)) &&
             rvx_decode(src, e->stored, out, e->size);
        if (src && !cached)
            osos_import_free(src);
    }
    if (!ok) {
        rvx_free(p, out);
        return NULL;
    }
    if (bytes)
        *bytes = e->size;
    return out;
}

/* An asset that cannot be kept in memory stays on disk and is read when used. */
void rvx_preload_assets(Pkg *p)
{
    for (u32 i = p->manifest->modules; i < p->entries; i++) {
        if (p->catalog[i].preload != 0)
            continue;
        if (!p->preloaded)
            p->preloaded = rvx_alloc(p, p->entries * sizeof(void *));
        if (!p->preloaded)
            return;
        const struct entry *e = p->catalog + i;
        uint8_t *data = rvx_alloc(p, e->stored);
        File *f;
        if (!data)
            continue;
        u32 got = 0;
        int status = rvx_open_entry(p, e, &f);
        if (status == OK) {
            status = rvx_file_read(p, f, data, e->stored, &got);
            rvx_file_close(p, f);
        }
        if (status == OK && got == e->stored)
            p->preloaded[i] = data;
        else
            rvx_free(p, data);
    }
}

struct stream {
    uint8_t codec;
    u32 stored, size;
};

struct provider {
    int (*replacement)(u32, u32, struct stream *);
    int (*read)(u32, u32, void *, u32);
    int (*adjust)(u32, u32, void *, u32);
};

static Pkg *provider_owner;
static const struct provider *provider;

int rvx_provider(Pkg *p, const void *new_provider)
{
    if (provider_owner && provider_owner != p)
        return ALREADY_BOUND;
    provider = new_provider;
    provider_owner = provider ? p : NULL;
    rvx_theme_changed();
    return OK;
}

/* The runtime's own bitmaps: payload/rvx/assets/<name>.png as menu masks,
 * lz-compressed behind a header and an entry table after the runtime image
 * in /Reprise/Firmware/reprise.bin, entry offsets relative to the trailer. Read
 * while shown, like an extension's optional asset; a theme's provider does not
 * see them. */
#define CORE_ASSETS_MAGIC 0x41435652u /* RVCA */
#define CORE_ASSETS_MAX 16
#define CORE_ASSET_MAX_BYTES (64u << 10)
#define CORE_ASSETS_MAX_BYTES (CORE_ASSETS_MAX * CORE_ASSET_MAX_BYTES)

struct core_assets_header {
    u32 magic, count, bytes, reserved;
};

extern const unsigned char __payload_limit[], __payload_start[];

static int file_read_exact(void *f, void *out, u32 n)
{
    u32 got;
    return !osos_file_read(f, out, n, &got) && got == n;
}

static void *core_bitmap_load(const char *name, u32 *n)
{
    struct osos_path path;
    void *f, *out = NULL;
    osos_path_init(&path, "/Reprise/Firmware/reprise.bin");
    if (osos_file_open(&path, 1, &f))
        return NULL;
    u32 runtime = CFW_PAYLOAD_FILE_OFFSET + (u32)(__payload_limit - __payload_start);
    u32 base = (runtime + rvx_trailer.stored + rvx_trailer.relocations * 2 + 3) & ~3u;
    u32 hash = rvx_hash(name);
    struct core_assets_header h;
    struct entry e;
    if (osos_file_seek(f, base, 0) || !file_read_exact(f, &h, sizeof h) ||
        h.magic != CORE_ASSETS_MAGIC || !h.count || h.count > CORE_ASSETS_MAX ||
        h.bytes > CORE_ASSETS_MAX_BYTES || h.bytes < sizeof h + h.count * sizeof e)
        goto done;
    for (u32 i = 0; i < h.count; i++) {
        if (!file_read_exact(f, &e, sizeof e))
            goto done;
        if (e.name == hash)
            break;
        if (i + 1 == h.count)
            goto done;
    }
    if (e.kind != 2 || e.codec > 1 || e.size < 28 || e.size > CORE_ASSET_MAX_BYTES ||
        !e.stored || e.offset > h.bytes || e.stored > h.bytes - e.offset ||
        (!e.codec && e.stored != e.size) || osos_file_seek(f, base + e.offset, 0))
        goto done;
    uint8_t *src = osos_import_alloc(e.stored);
    if (!src)
        goto done;
    if (file_read_exact(f, src, e.stored) && (out = rvx_alloc(NULL, e.size)) != NULL) {
        int ok = e.codec ? rvx_decode(src, e.stored, out, e.size) : 1;
        for (u32 i = 0; ok && !e.codec && i < e.size; i++)
            ((uint8_t *)out)[i] = src[i];
        if (!ok) {
            rvx_free(NULL, out);
            out = NULL;
        }
    }
    osos_import_free(src);
    if (out)
        *n = e.size;
done:
    osos_file_close(f);
    return out;
}

void *rvx_bitmap_load(Pkg *p, const char *name, u32 *n)
{
    if (!name)
        return NULL;
    if (!p)
        return core_bitmap_load(name, n);
    u32 owner = rvx_hash(p->id), id = rvx_hash(name);
    void *bitmap = NULL;
    if (provider && provider->replacement && provider->read) {
        struct stream s;
        if (provider->replacement(owner, id, &s) && s.size >= 28 &&
            s.size <= 16 * 1024 * 1024 && s.stored && s.stored <= 16 * 1024 * 1024 &&
            s.codec <= 1 && (s.codec || s.stored == s.size)) {
            uint8_t *raw = rvx_alloc(p, s.stored);
            if (raw && provider->read(owner, id, raw, s.stored)) {
                if (!s.codec) {
                    bitmap = raw;
                    raw = NULL;
                } else {
                    bitmap = rvx_alloc(p, s.size);
                    if (bitmap && !rvx_decode(raw, s.stored, bitmap, s.size)) {
                        rvx_free(p, bitmap);
                        bitmap = NULL;
                    }
                }
                if (bitmap)
                    *n = s.size;
            }
            if (raw)
                rvx_free(p, raw);
        }
    }
    if (!bitmap)
        bitmap = rvx_asset_load(p, name, n);
    if (bitmap && provider && provider->adjust)
        provider->adjust(owner, id, bitmap, *n);
    return bitmap;
}

void rvx_forget_provider(Pkg *p, const void *start, u32 bytes)
{
    if (p == provider_owner && (uintptr_t)provider - (uintptr_t)start < bytes) {
        provider = NULL;
        provider_owner = NULL;
        rvx_theme_changed();
    }
}
