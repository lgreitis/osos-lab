/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The runtime's layout assertions assume 32-bit pointers; this fixture runs on
 * the host, where the image addresses are truncated to 32 bits for the lookup. */
#define _Static_assert(...)
#include "../../../payload/rvx/internal.h"
#undef _Static_assert
#include "../../../payload/recovery.h"
#include "../../../payload/store.h"

u32 cfw_rvx_running;
Pkg *rvx_packages;

u32 rvx_hash(const char *s)
{
    u32 h = 2166136261u;
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static struct cfw_crash_mark stored_marks[CFW_CRASH_MARKS];
static uint32_t stored_bytes;
static uint32_t released, releases;

uint32_t cfw_store_get(uint32_t key, void *data, uint32_t capacity)
{
    assert(key == CFW_STORE_CRASHED);
    uint32_t size = stored_bytes < capacity ? stored_bytes : capacity;
    memcpy(data, stored_marks, size);
    return size;
}

void cfw_recovery_release(uint32_t hash)
{
    released = hash;
    releases++;
}

#include "../../../payload/rvx/crash.c"

/* Images are padded so that one past an image lies in no other image. */
static uint8_t images[4][512] __attribute__((aligned(16)));
static struct rvx_manifest manifests[2];
static struct rvx_module modules[2][2];
static char strings[2][64];
static Pkg *packages[2];

static Pkg *make(unsigned i, const char *id, const char *name, const char *path)
{
    Pkg *p = packages[i] = calloc(1, sizeof *p + strlen(path) + 1);
    strcpy(p->path, path);
    strcpy(p->id, id);
    p->owner = rvx_hash(id);
    p->bytes = 1000 + i;
    p->hash = 0x500 + i;
    p->state = 2;
    p->manifest = &manifests[i];
    manifests[i].modules = 2;
    manifests[i].text[0] = 0;
    manifests[i].text[1] = (uint16_t)(strlen(id) + 1);
    strcpy(strings[i], id);
    strcpy(strings[i] + strlen(id) + 1, name);
    manifests[i].strings = strlen(id) + 1 + strlen(name) + 1;
    p->strings = strings[i];
    p->modules = modules[i];
    for (unsigned m = 0; m < 2; m++) {
        modules[i][m].image = images[i * 2 + m];
        modules[i][m].size = 256;
    }
    return p;
}

static u32 at(unsigned image, unsigned offset)
{
    return (u32)(uintptr_t)images[image] + offset;
}

int main(void)
{
    Pkg *a = make(0, "alpha", "Alpha Tools", "/Reprise/Extensions/alpha.rvx");
    Pkg *b = make(1, "beta", "Beta", "/Reprise/Extensions/beta.rvx");
    a->next = b;
    rvx_packages = a;
    struct panic_frame f = {0};
    struct cfw_crash_owner out;

    /* The core: addresses in no image. */
    f.exception_lr = 0x08000000;
    assert(!rvx_crash_owner(&f, &out));

    /* Exception LR inside the second module of alpha. */
    f.exception_lr = at(1, 100);
    assert(rvx_crash_owner(&f, &out) && out.hash == rvx_hash("alpha"));
    assert(out.size == 1000 && out.content == 0x500);
    assert(!strcmp(out.id, "alpha") && !strcmp(out.name, "Alpha Tools") &&
           !strcmp(out.file, "alpha.rvx"));

    /* One past the image misses; the last byte hits. */
    f.exception_lr = at(1, 256);
    assert(!rvx_crash_owner(&f, &out));
    f.exception_lr = at(1, 255);
    assert(rvx_crash_owner(&f, &out) && out.hash == rvx_hash("alpha"));

    /* Registers only with a valid frame, then the stack. */
    f.exception_lr = 0;
    f.registers[7] = at(2, 4);
    assert(!rvx_crash_owner(&f, &out));
    f.valid = 1;
    assert(rvx_crash_owner(&f, &out) && out.hash == rvx_hash("beta"));
    f.registers[7] = 0;
    f.stack_count = 0xffffffff;
    f.stack[PANIC_STACK_WORDS - 1] = at(3, 8);
    assert(rvx_crash_owner(&f, &out) && out.hash == rvx_hash("beta"));
    f.stack_count = PANIC_STACK_WORDS - 1;
    assert(!rvx_crash_owner(&f, &out));

    /* The running marker wins over every address. */
    f.exception_lr = at(1, 0);
    cfw_rvx_running = rvx_hash("beta");
    assert(rvx_crash_owner(&f, &out) && out.hash == rvx_hash("beta"));
    cfw_rvx_running = rvx_hash("gamma");
    assert(rvx_crash_owner(&f, &out) && out.hash == rvx_hash("alpha"));
    cfw_rvx_running = 0;

    /* A released image is skipped. */
    modules[0][1].image = NULL;
    assert(!rvx_crash_owner(&f, &out));
    modules[0][1].image = images[1];

    /* A name past the string table reads as empty. */
    manifests[1].text[1] = 64;
    f.exception_lr = at(2, 0);
    assert(rvx_crash_owner(&f, &out) && !out.name[0]);

    /* Quarantine: the unchanged file is skipped, a changed or missing one
     * releases the mark. */
    rvx_quarantine();
    assert(a->state == 2 && b->state == 2 && !releases);
    stored_marks[0].hash = rvx_hash("beta");
    stored_marks[0].size = 1001;
    stored_marks[0].content = 0x501;
    stored_bytes = sizeof *stored_marks;
    rvx_quarantine();
    assert(b->state == 3 && a->state == 2 && !releases);
    b->state = 2;
    stored_marks[0].content = 0x502;
    rvx_quarantine();
    assert(b->state == 2 && released == rvx_hash("beta") && releases == 1);
    stored_marks[0].content = 0x501;
    stored_marks[0].size = 1002;
    rvx_quarantine();
    assert(b->state == 2 && released == rvx_hash("beta") && releases == 2);
    stored_marks[0].hash = rvx_hash("gamma");
    rvx_quarantine();
    assert(a->state == 2 && b->state == 2 && released == rvx_hash("gamma") && releases == 3);
    /* Several marks: each is judged on its own. */
    stored_marks[1].hash = rvx_hash("alpha");
    stored_marks[1].size = 1000;
    stored_marks[1].content = 0x500;
    stored_bytes = 2 * sizeof *stored_marks;
    rvx_quarantine();
    assert(a->state == 3 && b->state == 2 && released == rvx_hash("gamma") && releases == 4);
    a->state = 2;
    stored_bytes = 8;
    rvx_quarantine();
    assert(releases == 4 && a->state == 2);
    puts("PASS: crash owner lookup order, texts, quarantine marks");
    return 0;
}
