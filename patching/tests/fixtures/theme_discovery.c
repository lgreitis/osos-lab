/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define CFW_OSOS_H
#define CFW_THEME_DISCOVERY_HOST

struct osos_path {
    uint16_t volume;
    char text[258];
};

#include "../../../payload/theme_package.h"
#include "../../../payload/theme_id.c"

void cfw_theme_path(struct osos_path *out, const char *text)
{
    out->volume = 0;
    strcpy(out->text, text);
}

/* Fake native iterator: each returned name is a heap string, as on device. */
struct entry {
    const char *name;
    uint8_t is_directory;
};

struct cfw_directory_iterator {
    const struct entry *entries;
    unsigned at, live;
};

struct cfw_path_string {
    char *text;
    unsigned live;
};
static const struct entry *listing;
static unsigned iterators, strings, metadata_reads;
static char last_read[258];
static uint32_t appearance_mask = 1;

static void directory_init(struct cfw_directory_iterator *iterator, const char *path)
{
    assert(!strcmp(path, "themes"));
    iterator->entries = listing;
    iterator->at = 0;
    iterator->live = 1;
    iterators++;
}

static int directory_next(struct cfw_directory_iterator *iterator,
                          struct cfw_path_string *path, uint8_t *is_directory)
{
    assert(iterator->live && path->live && !path->text);
    const struct entry *e = &iterator->entries[iterator->at];
    if (!e->name)
        return 0;
    iterator->at++;
    path->text = strdup(e->name);
    *is_directory = e->is_directory;
    return 1;
}

static void directory_destroy(struct cfw_directory_iterator *iterator)
{
    assert(iterator->live);
    iterator->live = 0;
    iterators--;
}

static void path_string_init(struct cfw_path_string *path)
{
    path->text = NULL;
    path->live = 1;
    strings++;
}

static const char *path_string_text(struct cfw_path_string *path)
{
    assert(path->live && path->text);
    return path->text;
}

static void path_string_destroy(struct cfw_path_string *path)
{
    assert(path->live);
    free(path->text);
    path->text = NULL;
    path->live = 0;
    strings--;
}

/* Fake metadata: folder name decides validity and the stored id. */
int cfw_theme_metadata_read(const struct osos_path *path,
                            struct cfw_theme_metadata *out)
{
    assert(!path->volume && !strncmp(path->text, "themes/", 7));
    char folder[32];
    const char *slash = strchr(path->text + 7, '/');
    assert(slash && !strcmp(slash, "/theme.bin") && slash - path->text - 7 < 32);
    memcpy(folder, path->text + 7, slash - path->text - 7);
    folder[slash - path->text - 7] = 0;
    strcpy(last_read, path->text);
    metadata_reads++;
    if (!strncmp(folder, "bad", 3))
        return 0;
    memset(out, 0, sizeof(*out));
    out->magic = 0x344d5452;
    out->appearance_mask = appearance_mask;
    strcpy(out->id, strncmp(folder, "mismatch", 8) ? folder : "other");
    strcpy(out->name, folder);
    return 1;
}

#include "../../../payload/theme_discovery.c"

static uint32_t scan(const struct entry *list, struct cfw_theme_choice *out,
                     uint32_t capacity, int *overflow)
{
    listing = list;
    metadata_reads = 0;
    uint32_t count = cfw_theme_discovery_scan(out, capacity, overflow);
    assert(!iterators && !strings);
    return count;
}

static void select_theme(void)
{
    struct cfw_theme_metadata metadata, previous;
    struct osos_path path, previous_path;
    uint32_t dark;
    for (appearance_mask = 1; appearance_mask <= 3; appearance_mask++) {
        for (uint32_t preferred = 0; preferred <= 1; preferred++) {
            for (int fallback = 0; fallback <= 1; fallback++) {
                memset(&metadata, 0xa5, sizeof(metadata));
                memset(&path, 0xa5, sizeof(path));
                previous = metadata;
                previous_path = path;
                dark = 42;
                int supported = !!(appearance_mask & (1u << preferred));
                int ok = cfw_theme_discovery_select("modern", preferred, fallback,
                                                    &metadata, &path, &dark);
                assert(ok == (supported || fallback));
                if (ok) {
                    assert(dark == (supported ? preferred : preferred ^ 1));
                    assert(metadata.appearance_mask == appearance_mask);
                    assert(!strcmp(metadata.id, "modern") && !path.volume);
                    assert(!strcmp(path.text, dark ? "themes/modern/dark.bin"
                                                   : "themes/modern/light.bin"));
                } else {
                    assert(!memcmp(&metadata, &previous, sizeof(metadata)));
                    assert(!memcmp(&path, &previous_path, sizeof(path)));
                    assert(dark == 42);
                }
            }
        }
    }
    appearance_mask = 1;
    const char *invalid[] = {NULL,   "",        "../modern", "default",
                             "dark", "classic", "badmeta",   "mismatch"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        previous = metadata;
        previous_path = path;
        uint32_t previous_dark = dark;
        assert(!cfw_theme_discovery_select(invalid[i], 0, 1, &metadata, &path, &dark));
        assert(!memcmp(&metadata, &previous, sizeof(metadata)));
        assert(!memcmp(&path, &previous_path, sizeof(path)) && dark == previous_dark);
    }
    unsigned reads = metadata_reads;
    assert(!cfw_theme_discovery_select("modern", 2, 1, &metadata, &path, &dark));
    assert(!cfw_theme_discovery_select("modern", 0, 1, NULL, &path, &dark));
    assert(!cfw_theme_discovery_select("modern", 0, 1, &metadata, NULL, &dark));
    assert(!cfw_theme_discovery_select("modern", 0, 1, &metadata, &path, NULL));
    assert(metadata_reads == reads);

    /* A fresh metadata read observes a package changed since discovery. */
    appearance_mask = 3;
    assert(cfw_theme_discovery_select("modern", 1, 0, &metadata, &path, &dark));
    appearance_mask = 1;
    assert(!cfw_theme_discovery_select("modern", 1, 0, &metadata, &path, &dark));
    assert(metadata_reads == reads + 2);
}

int main(void)
{
    select_theme();
    struct osos_path path;
    assert(cfw_theme_discovery_path("modern", "dark.bin", &path));
    assert(!path.volume && !strcmp(path.text, "themes/modern/dark.bin"));
    assert(cfw_theme_discovery_path("a-1_b", "theme.bin", &path));
    const char *unsafe[] = {"",
                            ".",
                            "..",
                            "-a",
                            "_a",
                            "A",
                            "a/b",
                            "a\\b",
                            "a.b",
                            "a b",
                            "abcdefghijklmnopqrstuvwxyz0123456"};
    for (unsigned i = 0; i < sizeof(unsafe) / sizeof(*unsafe); i++)
        assert(!cfw_theme_discovery_path(unsafe[i], "theme.bin", &path));
    assert(cfw_theme_discovery_path("abcdefghijklmnopqrstuvwxyz01234", "theme.bin",
                                    &path));

    static const struct entry mixed[] = {
        {"zeta", 1},     {"file", 0},     {".", 1},
        {"..", 1},       {"badUpper", 1}, {"default", 1},
        {"dark", 1},     {"badmeta", 1},  {"classic", 1},
        {"mismatch", 1}, {"alpha", 1},    {"has space", 1},
        {"-dash", 1},    {"m-id_2", 1},   {"abcdefghijklmnopqrstuvwxyz0123456", 1},
        {NULL, 0},
    };
    struct cfw_theme_choice out[8];
    int overflow = -1;
    assert(scan(mixed, out, 8, &overflow) == 3 && !overflow);
    assert(!strcmp(out[0].id, "alpha") && !strcmp(out[1].id, "m-id_2") &&
           !strcmp(out[2].id, "zeta") && !strcmp(out[1].name, "m-id_2"));
    /* Only safe, non-reserved directories reach theme.bin. */
    assert(metadata_reads == 6);

    static const struct entry fat_names[] = {
        {"DEFAULT", 1},   {"DARK", 1}, {"MODERN", 1}, {"Night", 1}, {"MODERN", 1},
        {"../MODERN", 1}, {"A/B", 1},  {"A\\B", 1},   {NULL, 0},
    };
    assert(scan(fat_names, out, 8, &overflow) == 2 && !overflow);
    assert(!strcmp(out[0].id, "modern") && !strcmp(out[1].id, "night"));
    assert(metadata_reads == 3);

    /* Overflow keeps the smallest ids regardless of enumeration order. */
    static const struct entry forward[] = {
        {"a", 1}, {"b", 1}, {"c", 1}, {"d", 1}, {"e", 1}, {NULL, 0},
    };
    static const struct entry reverse[] = {
        {"e", 1}, {"d", 1}, {"badx", 1}, {"c", 1}, {"b", 1}, {"a", 1}, {NULL, 0},
    };
    static const struct entry shuffled[] = {
        {"c", 1}, {"e", 1}, {"a", 1}, {"d", 1}, {"b", 1}, {NULL, 0},
    };
    const struct entry *orders[] = {forward, reverse, shuffled};
    for (unsigned o = 0; o < 3; o++) {
        memset(out, 0xa5, sizeof(out));
        overflow = 0;
        assert(scan(orders[o], out, 3, &overflow) == 3 && overflow);
        assert(!strcmp(out[0].id, "a") && !strcmp(out[1].id, "b") &&
               !strcmp(out[2].id, "c"));
        assert((unsigned char)out[3].id[0] == 0xa5); /* nothing written past capacity */
    }
    overflow = -1;
    assert(scan(forward, out, 5, &overflow) == 5 && !overflow);
    assert(scan(forward, out, 5, NULL) == 5);
    assert(scan(forward, NULL, 0, &overflow) == 0 && overflow);
    assert(metadata_reads == 1);
    static const struct entry probe_skip[] = {{"badx", 1}, {"file", 0}, {"b", 1}, {"c", 1},
                                              {NULL, 0}};
    overflow = -1;
    assert(scan(probe_skip, NULL, 0, &overflow) == 0 && overflow);
    assert(metadata_reads == 2 && !strcmp(last_read, "themes/b/theme.bin"));

    /* Invalid entries never count toward overflow. */
    static const struct entry invalid_tail[] = {
        {"a", 1}, {"b", 1}, {"badz", 1}, {"mismatch", 1}, {"z", 0}, {NULL, 0},
    };
    overflow = -1;
    assert(scan(invalid_tail, out, 2, &overflow) == 2 && !overflow);

    static const struct entry duplicate[] = {{"a", 1}, {"a", 1}, {NULL, 0}};
    assert(scan(duplicate, out, 2, &overflow) == 1 && !overflow);

    /* Filter a larger directory while keeping the smallest valid IDs. */
    struct entry many[42];
    char names[41][16];
    for (unsigned i = 0; i < 41; i++) {
        snprintf(names[i], sizeof(names[i]), i % 2 ? "bad%02u" : "theme%02u", i);
        many[i] = (struct entry){names[i], 1};
    }
    many[41] = (struct entry){NULL, 0};
    assert(scan(many, out, 8, &overflow) == 8 && overflow);
    assert(metadata_reads == 41);
    assert(!strcmp(out[0].id, "theme00") && !strcmp(out[7].id, "theme14"));
    scan(duplicate, out, 2, &overflow);

    static const struct entry empty[] = {{NULL, 0}};
    overflow = -1;
    assert(scan(empty, out, 8, &overflow) == 0 && !overflow);
    assert(!strcmp(last_read, "themes/a/theme.bin"));
    return 0;
}
