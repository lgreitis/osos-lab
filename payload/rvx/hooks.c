/* SPDX-License-Identifier: GPL-3.0-only */
#include "internal.h"
#include "host.h"
#include "patch.h"
#include "osos.h"

/* CALL: a BL to a function; VTABLE: a method slot; ENTRY: a function's first
 * word; WORD: a table entry or constant; INSERT: the dispatcher's own branch;
 * BRANCH: a B over a one-word tail-call thunk, handled like a CALL because lr
 * already belongs to the thunk's caller. */
enum { CALL, VTABLE, ENTRY, ENTRY_NORETURN, WORD, INSERT, BRANCH };

struct site {
    u32 id, address, word;
    u32 kind : 3, chain : 1, argc : 8, copy : 20;
};
static const struct site named[] = {
#include "site_table.inc"
};

enum { NAMED = sizeof named / sizeof named[0], SLOTS = RVX_HOOK_SLOTS, BINDINGS = 256 };

_Static_assert(NAMED < SLOTS, "RVX vectors");

struct binding {
    struct binding *next;
    Pkg *pkg;
    const struct rvx_hook *hook;
    u32 fn;
    int16_t priority;
    uint8_t mode, retired;
};

struct chain {
    u32 address, kind : 3, argc : 8, copy : 20;
    struct binding *head;
    u32 original, previous, replay[3], patched;
};

enum { BINDING_PAGE = 8 };

static void *binding_pages[BINDINGS / BINDING_PAGE];

static unsigned active;
extern void rvx_insert_vector(void);
extern const u32 rvx_vectors[][2], __rvx_reserved_start[], __rvx_reserved_end[];

static int reserved(u32 a)
{
    for (const u32 *p = __rvx_reserved_start; p < __rvx_reserved_end; p++)
        if (a - (*p & ~1u) < ((*p & 1) ? 8u : 4u))
            return 1;
    return 0;
}

static int locate(Pkg *p, u32 id, struct site *out)
{
    if (id < 0xffffff00u) {
        for (unsigned i = 0; i < NAMED; i++)
            if (named[i].id == id) {
                *out = named[i];
                return i;
            }
        return -1;
    }
    unsigned index = id - 0xffffff00u;
    if (!p || index >= p->manifest->sites)
        return -1;
    const struct rvx_site *s = p->sites + index;
    if (s->address < 0x08000000 || s->address >= 0x08b3b1b0 || (s->address & 3) ||
        s->argc > 4 || (s->kind != CALL && s->kind != VTABLE && s->kind != WORD) ||
        (s->kind == CALL && s->expected >> 24 != 0xeb) ||
        (s->kind == WORD && s->argc) || reserved(s->address))
        return -1;
    for (unsigned i = 0; i < NAMED; i++)
        if (named[i].address == s->address)
            return -1;
    int empty = -1;
    for (unsigned i = NAMED; i < SLOTS; i++) {
        struct chain *c = rvx_chains[i];
        if (c && c->head && c->address == s->address) {
            if (c->head->pkg != p)
                return -1;
            *out = (struct site){id, s->address, s->expected, s->kind, 0, s->argc, 0};
            return i;
        }
        if ((!c || !c->head) && empty < 0)
            empty = i;
    }
    *out = (struct site){id, s->address, s->expected, s->kind, 0, s->argc, 0};
    return empty;
}

static int owned(Pkg *p, u32 fn)
{
    for (unsigned i = 0; i < p->manifest->modules; i++) {
        struct rvx_module *m = p->modules + i;
        if (m->image && (fn & ~1u) - (uintptr_t)m->image < m->text && (fn & 3) != 2)
            return 1;
    }
    return 0;
}

static int prepare(struct chain *c, const struct site *s, unsigned slot, u32 fn)
{
    u32 word = *(volatile u32 *)s->address;
    int ours = reserved(s->address);
    if (word != s->word && !ours)
        return 0;
    u32 vector = (u32)rvx_vectors[slot];
    u32 branch = ((vector - s->address - 8) >> 2) & 0xffffff;
    c->address = s->address;
    c->kind = s->kind;
    c->argc = s->argc;
    c->copy = s->copy;
    c->previous = word;
    c->original = 0;
    switch (s->kind) {
    case INSERT:
        rvx_insert_slot = slot;
        c->patched =
            0xea000000 | ((((u32)rvx_insert_vector - s->address - 8) >> 2) & 0xffffff);
        break;
    case WORD:
        /* Named word slots may overlay Reprise's version of the same table
         * entry or constant. A two-word firmware jump must stay intact. */
        if (ours) {
            for (const u32 *r = __rvx_reserved_start; r < __rvx_reserved_end; r++)
                if ((*r & 1) && s->address - (*r & ~1u) < 8)
                    return 0;
        }
        c->patched = fn;
        break;
    case CALL:
        if (word >> 24 == 0xeb)
            c->original = s->address + 8 + ((int32_t)(word << 8) >> 6);
        else if ((word & 0xfffffff0) != 0xe12fff30 && (word != s->word || s->chain))
            return 0;
        c->patched = 0xeb000000 | branch;
        break;
    case BRANCH:
        if (word >> 24 != 0xea)
            return 0;
        c->original = s->address + 8 + ((int32_t)(word << 8) >> 6);
        c->patched = 0xea000000 | branch;
        break;
    case VTABLE:
        c->original = word;
        c->patched = vector;
        break;
    case ENTRY:
    case ENTRY_NORETURN:
        if (ours && word == 0xe51ff004)
            c->original = *(volatile u32 *)(s->address + 4);
        else if (word == s->word) {
            c->replay[0] = word;
            c->replay[1] = 0xe51ff004;
            c->replay[2] = s->address + 4;
            cfw_rvx_sync(c->replay, c->replay + 3);
            c->original = (u32)c->replay;
        } else
            return 0;
        c->patched = 0xea000000 | branch;
        break;
    default:
        return 0;
    }
    return 1;
}

/* Publish cold storage under the hook lock, but let the native allocator run
 * outside our critical section. Another caller can populate the same slot. */
static int grow(void *volatile *slot, u32 bytes)
{
    void *storage = osos_import_alloc(bytes);
    u32 irq = cfw_rvx_lock();
    if (!*slot && storage) {
        *slot = storage;
        storage = NULL;
    }
    int available = *slot != NULL;
    cfw_rvx_unlock(irq);
    if (storage)
        osos_import_free(storage);
    return available;
}

/* Core hooks have no owning extension and use only named sites. */
static int bind_hook(Pkg *p, const struct rvx_hook *h)
{
    if (!h || h->reserved || h->mode > 1)
        return BAD_ARGUMENT;
retry:;
    struct site s;
    u32 irq = cfw_rvx_lock();
    int slot = locate(p, h->site, &s), result = BAD_SITE;
    if (slot < 0)
        goto done;
    result = BAD_ARGUMENT;
    if (s.kind == WORD ? !h->mode : (p && !owned(p, h->value)))
        goto done;
    struct chain *c = rvx_chains[slot];
    if (!c) {
        cfw_rvx_unlock(irq);
        if (grow(&rvx_chains[slot], sizeof *c))
            goto retry;
        return NO_MEMORY;
    }
    struct binding **insert = &c->head;
    for (struct binding *b = c->head; b; b = b->next) {
        if (b->pkg == p && b->fn == h->value) {
            result = ALREADY_BOUND;
            goto done;
        }
        if (!s.chain || h->mode || b->mode) {
            result = CONFLICT;
            goto done;
        }
        if (b->priority <= h->priority)
            insert = &b->next;
    }
    struct binding *b = NULL;
    for (unsigned page = 0; page < BINDINGS / BINDING_PAGE && !b; page++) {
        struct binding *first = binding_pages[page];
        if (!first) {
            cfw_rvx_unlock(irq);
            if (grow(&binding_pages[page], BINDING_PAGE * sizeof *b))
                goto retry;
            return NO_MEMORY;
        }
        for (unsigned i = 0; i < BINDING_PAGE; i++)
            if (!first[i].hook && (!active || !first[i].retired)) {
                b = first + i;
                break;
            }
    }
    if (!b) {
        result = NO_MEMORY;
        goto done;
    }
    int first = !c->head;
    if (first && !prepare(c, &s, slot, h->value)) {
        result = CONFLICT;
        goto done;
    }
    *b = (struct binding){*insert, p, h, h->value, h->priority, h->mode, 0};
    *insert = b;
    if (first)
        cfw_rvx_write(s.address, c->patched);
    result = OK;
done:
    cfw_rvx_unlock(irq);
    return result;
}

int rvx_bind(Pkg *p, const struct rvx_hook *h)
{
    return p ? bind_hook(p, h) : BAD_ARGUMENT;
}

void rvx_bind_core(const struct rvx_hook *h)
{
    bind_hook(NULL, h);
}

static void remove_binding(struct chain *c, struct binding **it)
{
    struct binding *b = *it;
    *it = b->next;
    b->retired = 1;
    b->hook = NULL;
    if (!c->head && *(volatile u32 *)c->address == c->patched)
        cfw_rvx_write(c->address, c->previous);
}

int rvx_unbind(Pkg *p, const struct rvx_hook *h)
{
    if (!p || !h)
        return BAD_ARGUMENT;
    for (unsigned j = 0; j < p->manifest->modules; j++) {
        const struct rvx_descriptor *d = p->modules[j].desc;
        if (d && (uintptr_t)h - (uintptr_t)d->hooks < d->count * sizeof *h)
            return NOT_BOUND;
    }
    u32 irq = cfw_rvx_lock();
    int result = NOT_BOUND;
    for (unsigned i = 0; i < SLOTS; i++) {
        struct chain *c = rvx_chains[i];
        if (!c)
            continue;
        for (struct binding **it = &c->head; *it; it = &(*it)->next)
            if ((*it)->pkg == p && (*it)->hook == h) {
                remove_binding(c, it);
                result = OK;
                goto done;
            }
    }
done:
    cfw_rvx_unlock(irq);
    return result;
}

void rvx_remove_hooks(Pkg *p, const struct rvx_module *m)
{
    u32 irq = cfw_rvx_lock();
    for (unsigned i = 0; i < SLOTS; i++) {
        struct chain *c = rvx_chains[i];
        if (!c)
            continue;
        for (struct binding **it = &c->head; *it;) {
            struct binding *b = *it;
            if (b->pkg == p && ((u32)b->hook - (u32)m->image < m->size ||
                                (b->fn & ~1u) - (u32)m->image < m->text))
                remove_binding(c, it);
            else
                it = &b->next;
        }
    }
    cfw_rvx_unlock(irq);
}

struct continuation {
    struct binding *binding;
    u32 original, argc;
};

typedef u32 (*Fn)(u32, u32, u32, u32, u32, u32, u32, u32);

static PATCH_ARM __attribute__((optimize("no-optimize-sibling-calls"))) u32
invoke(const struct continuation *ctx, u32 args[8])
{
    struct binding *b = ctx->binding;
    while (b && b->retired)
        b = b->next;
    u32 fn = ctx->original;
    struct continuation next;
    if (b) {
        next = (struct continuation){b->next, ctx->original, ctx->argc};
        fn = b->fn;
        for (unsigned i = 7; i > ctx->argc; i--)
            args[i] = args[i - 1];
        args[ctx->argc] = (u32)&next;
    }
    return fn ? ((Fn)fn)(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                         args[7])
              : 0;
}

u32 rvx_next7(const void *ctx, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g)
{
    u32 args[] = {a, b, c, d, e, f, g, 0};
    return invoke(ctx, args);
}

u32 rvx_next(const void *ctx, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f)
{
    return rvx_next7(ctx, a, b, c, d, e, f, 0);
}

u32 rvx_dispatch(u32 *regs, unsigned slot)
{
    struct chain *c = rvx_chains[slot];
    unsigned argc = c->argc & 15, stack = c->argc >> 4;
    u32 copy = c->copy, args[8];
    args[4] = args[5] = args[6] = args[7] = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned r = (copy >> (4 * i)) & 15;
        args[i] = regs[r ? r : i];
    }
    for (unsigned i = 0; i < stack; i++)
        args[4 + i] = regs[14 + i];
    if (copy >> 16)
        args[4 + stack] = regs[copy >> 16];
    u32 irq = cfw_rvx_lock();
    active++;
    struct continuation ctx = {c->head, c->original, argc};
    if (c->kind == CALL && (c->previous & 0xfffffff0) == 0xe12fff30)
        ctx.original = regs[c->previous & 15];
    cfw_rvx_unlock(irq);
    u32 result = invoke(&ctx, args);
    irq = cfw_rvx_lock();
    active--;
    cfw_rvx_unlock(irq);
    return result;
}
