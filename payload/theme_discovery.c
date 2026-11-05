/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_discovery.h"
#include "osos.h"

#ifndef CFW_THEME_DISCOVERY_HOST
/* Synchronous iterator behind AsyncFileIO; same entries as game-sdk/runtime/native.h. */
struct cfw_directory_iterator {
    uint32_t state[0x44 / 4];
};

struct cfw_path_string {
    uint32_t vtable_address, data_address;
};

_Static_assert(sizeof(struct cfw_directory_iterator) == 0x44,
               "directory iterator size");
_Static_assert(sizeof(struct cfw_path_string) == 8, "native path string size");

static inline void directory_init(struct cfw_directory_iterator *iterator,
                                  const char *path)
{
    typedef void *(*fn)(void *, const char *, uint8_t, int);
    ((fn)0x081e57dc)(iterator, path, 0, 0);
}

static inline int directory_next(struct cfw_directory_iterator *iterator,
                                 struct cfw_path_string *path, uint8_t *is_directory)
{
    typedef int (*fn)(void *, void *, uint8_t *);
    return ((fn)0x081e54e8)(iterator, path, is_directory);
}

static inline void directory_destroy(struct cfw_directory_iterator *iterator)
{
    typedef void (*fn)(void *);
    ((fn)0x081e5928)(iterator);
}

static inline void path_string_init(struct cfw_path_string *path)
{
    typedef void (*fn)(void *);
    ((fn)0x0826fcc4)(path);
}

static inline const char *path_string_text(struct cfw_path_string *path)
{
    typedef const char *(*fn)(void *);
    return ((fn)0x0829bac0)(path);
}

static inline void path_string_destroy(struct cfw_path_string *path)
{
    typedef void (*fn)(void *);
    ((fn)0x0826fcf4)(path);
}
#endif

#define THEMES_ROOT "themes"

/* Same rule as theme.bin ids: [a-z0-9][a-z0-9_-]{0,30}. */
static int safe_id(const char *id)
{
    unsigned i = 0;
    for (; i < sizeof(((struct cfw_theme_metadata *)0)->id) && id[i]; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              (i && (c == '_' || c == '-'))))
            return 0;
    }
    return i && i < sizeof(((struct cfw_theme_metadata *)0)->id);
}

static int compare(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* FAT short directory names are returned uppercase by the native iterator. */
static int directory_id(const char *name, char *id)
{
    unsigned i = 0;
    for (; i < sizeof(((struct cfw_theme_metadata *)0)->id); i++) {
        char c = name[i];
        id[i] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
        if (!c)
            return safe_id(id);
    }
    return 0;
}

/* Explicit byte loop: the freestanding ARM payload has no memcpy. */
static void copy_entry(struct cfw_theme_choice *destination,
                       const struct cfw_theme_choice *source)
{
    unsigned char *p = (unsigned char *)destination;
    const unsigned char *q = (const unsigned char *)source;
    for (uint32_t i = 0; i < sizeof(*destination); i++)
        p[i] = q[i];
}

static int append(struct osos_path *out, unsigned *at, const char *text)
{
    for (; *text; text++) {
        if (*at + 1 >= sizeof(out->text))
            return 0;
        out->text[(*at)++] = *text;
    }
    out->text[*at] = 0;
    return 1;
}

int cfw_theme_discovery_path(const char *id, const char *file, struct osos_path *out)
{
    unsigned at = 0;
    out->volume = 0;
    return safe_id(id) && append(out, &at, THEMES_ROOT "/") && append(out, &at, id) &&
           append(out, &at, "/") && append(out, &at, file);
}

/* Sorted insert into a bounded array; the largest id falls off when full. */
static uint32_t insert(struct cfw_theme_choice *entries, uint32_t capacity,
                       uint32_t count, const struct cfw_theme_choice *candidate,
                       int *overflow)
{
    uint32_t at = 0;
    while (at < count && compare(entries[at].id, candidate->id) < 0)
        at++;
    if (at < count && !compare(entries[at].id, candidate->id))
        return count;
    if (count == capacity) {
        *overflow = 1;
        if (at == capacity)
            return count;
        count--;
    }
    for (uint32_t i = count; i > at; i--)
        copy_entry(&entries[i], &entries[i - 1]);
    copy_entry(&entries[at], candidate);
    return count + 1;
}

uint32_t cfw_theme_discovery_scan(struct cfw_theme_choice *entries, uint32_t capacity,
                                  int *overflow)
{
    int overflowed = 0;
    uint32_t count = 0;
    struct cfw_directory_iterator iterator;
    directory_init(&iterator, THEMES_ROOT);
    for (;;) {
        struct cfw_path_string name;
        uint8_t is_directory = 0;
        path_string_init(&name);
        int more = directory_next(&iterator, &name, &is_directory);
        const char *text = more ? path_string_text(&name) : 0;
        struct osos_path path;
        struct cfw_theme_metadata candidate;
        char id[sizeof(candidate.id)];
        /* Built-in and legacy folders are not custom packages. */
        int valid =
            text && is_directory && directory_id(text, id) && compare(id, "default") &&
            compare(id, "dark") && compare(id, "classic") &&
            cfw_theme_discovery_path(id, "theme.bin", &path) &&
            cfw_theme_metadata_read(&path, &candidate) && !compare(id, candidate.id);
        path_string_destroy(&name);
        if (!more)
            break;
        if (valid) {
            struct cfw_theme_choice choice;
            for (unsigned i = 0; i < sizeof(choice.id); i++)
                choice.id[i] = candidate.id[i];
            for (unsigned i = 0; i < sizeof(choice.name); i++)
                choice.name[i] = candidate.name[i];
            count = insert(entries, capacity, count, &choice, &overflowed);
            if (!capacity)
                break;
        }
    }
    directory_destroy(&iterator);
    if (overflow)
        *overflow = overflowed;
    return count;
}

int cfw_theme_discovery_select(const char *id, uint32_t prefer_dark, int allow_fallback,
                               struct cfw_theme_metadata *metadata,
                               struct osos_path *pack, uint32_t *dark)
{
    if (!id || !metadata || !pack || !dark || prefer_dark > 1 || !safe_id(id) ||
        !compare(id, "default") || !compare(id, "dark") || !compare(id, "classic"))
        return 0;
    struct osos_path path;
    struct cfw_theme_metadata candidate;
    if (!cfw_theme_discovery_path(id, "theme.bin", &path) ||
        !cfw_theme_metadata_read(&path, &candidate) || compare(id, candidate.id))
        return 0;
    uint32_t selected = prefer_dark;
    if (!(candidate.appearance_mask & (1u << selected))) {
        if (!allow_fallback)
            return 0;
        selected ^= 1;
    }
    if (!(candidate.appearance_mask & (1u << selected)) ||
        !cfw_theme_discovery_path(id, selected ? "dark.bin" : "light.bin", &path))
        return 0;
    for (unsigned i = 0; i < sizeof(candidate); i++)
        ((unsigned char *)metadata)[i] = ((const unsigned char *)&candidate)[i];
    pack->volume = path.volume;
    unsigned i = 0;
    do {
        pack->text[i] = path.text[i];
    } while (path.text[i++]);
    *dark = selected;
    return 1;
}
