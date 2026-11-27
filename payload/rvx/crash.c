/* SPDX-License-Identifier: GPL-3.0-only */
#include "internal.h"
#include "recovery.h"
#include "store.h"

/* The lookups run inside the exception handler over a heap the crash may have
 * damaged; the walk stops after this many packages. */
enum { WALK_LIMIT = 64 };

/* The package whose loaded module image holds `address`. */
static Pkg *owner_of(u32 address)
{
    unsigned walked = 0;
    for (Pkg *p = rvx_packages; p && walked < WALK_LIMIT; p = p->next, walked++) {
        if (!p->manifest || !p->modules)
            continue;
        for (u32 i = 0; i < p->manifest->modules; i++) {
            const struct rvx_module *m = p->modules + i;
            if (m->image && address - (u32)(uintptr_t)m->image < m->size)
                return p;
        }
    }
    return NULL;
}

static Pkg *running(void)
{
    unsigned walked = 0;
    for (Pkg *p = rvx_packages; cfw_rvx_running && p && walked < WALK_LIMIT;
         p = p->next, walked++)
        if (p->owner == cfw_rvx_running && p->manifest)
            return p;
    return NULL;
}

static void copy_text(char *out, const char *in, u32 capacity)
{
    u32 i = 0;
    for (; i + 1 < capacity && in && in[i]; i++)
        out[i] = in[i];
    out[i] = 0;
}

/* The manifest string at `offset`, NULL when it runs past the string table. */
static const char *manifest_text(const Pkg *p, u32 offset)
{
    for (u32 i = offset; i < p->manifest->strings; i++)
        if (!p->strings[i])
            return p->strings + offset;
    return NULL;
}

int rvx_crash_owner(const struct panic_frame *f, struct cfw_crash_owner *out)
{
    Pkg *p = running();
    if (!p)
        p = owner_of(f->exception_lr);
    for (u32 i = 0; !p && f->valid && i < 13; i++)
        p = owner_of(f->registers[i]);
    for (u32 i = 0; !p && i < f->stack_count && i < PANIC_STACK_WORDS; i++)
        p = owner_of(f->stack[i]);
    if (!p)
        return 0;
    out->hash = p->owner;
    out->size = p->bytes;
    out->content = p->hash;
    copy_text(out->id, p->id, sizeof out->id);
    copy_text(out->name, manifest_text(p, p->manifest->text[1]), sizeof out->name);
    const char *file = p->path;
    for (const char *c = p->path; *c; c++)
        if (*c == '/')
            file = c + 1;
    copy_text(out->file, file, sizeof out->file);
    return 1;
}

void rvx_quarantine(void)
{
    struct cfw_crash_mark marks[CFW_CRASH_MARKS];
    unsigned count = cfw_store_get(CFW_STORE_CRASHED, marks, sizeof marks) / sizeof *marks;
    for (unsigned i = 0; i < count; i++) {
        Pkg *p = rvx_packages;
        while (p && p->owner != marks[i].hash)
            p = p->next;
        if (p && p->bytes == marks[i].size && p->hash == marks[i].content)
            p->state = 3;
        else
            cfw_recovery_release(marks[i].hash);
    }
}
