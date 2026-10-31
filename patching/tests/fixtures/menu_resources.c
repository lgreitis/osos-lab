/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM
#define osos_resource_block_append append_block
#define OSOS_RESOURCE_BMAP 0x424d6170

struct osos_string {
    const char *data;
};

struct block {
    uint32_t size, metadata;
    void *data;
};

struct list {
    uint32_t selectors[4];
    struct block *blocks[8];
    unsigned count;
};

static unsigned allocations;

static void append_block(void *object, void *data, uint32_t size, uint32_t metadata)
{
    struct list *list = object;
    assert(list->count < 8);
    struct block *block = malloc(sizeof(*block));
    assert(block);
    *block = (struct block){size, metadata, data};
    list->blocks[list->count++] = block;
    allocations++;
}

static void destroy(struct list *list)
{
    for (unsigned i = 0; i < list->count; i++) {
        free(list->blocks[i]);
        allocations--;
    }
    list->count = 0;
}

void cfw_ui_register_controllers(void)
{
}

void cfw_ui_register_canvas(void)
{
}

static void osos_resource_set(void *, uint32_t, uint32_t, const void *, uint32_t);

static const void *osos_resource_next(void *bank, uint32_t *id, uint32_t *size)
{
    (void)bank;
    (void)id;
    (void)size;
    return NULL;
}

static void osos_string_init(struct osos_string *s, const char *text)
{
    s->data = text;
}

static void osos_string_destroy(struct osos_string *s)
{
    (void)s;
}

static uint32_t name_id;
static int language = -1;
static unsigned language_queries, translation_probes;
static int osos_ui_language(void) { language_queries++; return language; }
static uint32_t *osos_resource_name_slot(const struct osos_string *s)
{
    (void)s;
    return &name_id;
}
#include "../../../payload/resources.h"
static const struct cfw_translation *translation_table(void);
#define cfw_translations translation_table()
#include "../../../payload/resources.c"
#undef cfw_translations

static unsigned resource_lookups;
const void *cfw_resource_service_get_original(void *service, uint32_t type, uint32_t id)
{
    (void)service;
    (void)type;
    (void)id;
    resource_lookups++;
    return "Stock";
}

enum { ARTISTS = 1, ITUNES_U, NOW_PLAYING, ALBUM_ARTISTS, SECOND };

struct row {
    uint32_t prefix[12], id, padding[12], visible;
};
static struct row stock[] = {
    {.id = ARTISTS}, {.id = ITUNES_U, .visible = 1}, {.id = NOW_PLAYING}};
static struct row added[] = {{.id = ALBUM_ARTISTS},
                             {.id = SECOND, .visible = 1},
                             {.id = ALBUM_ARTISTS, .visible = 1}};
const struct cfw_ui_menu_item cfw_ui_menu_items[] = {
    {26, ARTISTS, sizeof(struct row), 7, (unsigned char *)&added[0]},
    {26, ARTISTS, sizeof(struct row), 8, (unsigned char *)&added[1]},
    {27, ARTISTS, sizeof(struct row), 9, (unsigned char *)&added[2]},
};
const uint32_t cfw_ui_menu_item_count = 3;
static const unsigned char icon[] = {8, 0, 4, 0};
static const unsigned char label[] = "Albums";
const struct cfw_resource cfw_resources[] = {
    {0x53747220, 99, sizeof(label), label},
    {OSOS_RESOURCE_BMAP, 26, sizeof(icon), icon},
    {0x53747220, 200, 14, (const unsigned char *)"%1$s: %2$u%%"},
    {0x53747220, 201, 3, (const unsigned char *)"%s"},
    {0x53747220, 202, 3, (const unsigned char *)"%u"},
    {0x53747220, 203, 8, (const unsigned char *)"100%%!"},
};
const uint32_t cfw_resource_count = 6;
const struct cfw_resource_name cfw_resource_names[] = {{"AlbumArtists", ALBUM_ARTISTS}};
const uint32_t cfw_resource_name_count = 1;
static unsigned installed;
static int bank;
const struct cfw_translation cfw_translations[] = {
    {100, 2, (const unsigned char *)"Francais"},
    {100, 4, (const unsigned char *)"Deutsch"},
    {101, 0, (const unsigned char *)"English"},
    {200, 4, (const unsigned char *)"%2$u%% - %1$s %1$s"},
};
const uint32_t cfw_translation_count = 4;
static const struct cfw_translation *translation_table(void)
{
    translation_probes++;
    return cfw_translations;
}
const struct cfw_format_spec cfw_formats[] = {
    {200, "su"}, {201, "s"}, {202, "u"}, {203, ""},
};
const uint32_t cfw_format_count = 4;

static void osos_resource_set(void *target, uint32_t type, uint32_t id,
                              const void *data, uint32_t size)
{
    assert(target == &bank && type == 0x53747220 && data && size);
    assert(id == 99 || (id >= 200 && id <= 203));
    if (id == 99)
        assert(data == label && size == sizeof(label));
    installed++;
}

static void load(struct list *list, uint32_t table)
{
    *list = (struct list){.selectors = {0, 0, 0x4954454d, table}};
    for (unsigned i = 0; i < 3; i++)
        cfw_ui_append_resource_block(list, &stock[i], sizeof(stock[i]), 0);
}

int main(void)
{
    struct list registration;
    struct row *registry[6] = {0};
    /* Native registration precedes override installation and borrows the same rows. */
    load(&registration, 26);
    assert(registration.count == 5);
    for (unsigned i = 0; i < registration.count; i++) {
        struct row *row = registration.blocks[i]->data;
        registry[row->id] = row;
    }
    destroy(&registration);
    assert(!allocations);
    for (language = -1; language <= 21; language++) {
        for (uint32_t id = 0; id <= 201; id++) {
            const void *expected = NULL;
            int known_id = 0;
            for (uint32_t index = 0; index < cfw_translation_count; index++) {
                const struct cfw_translation *translation = &cfw_translations[index];
                if (translation->id == id) {
                    known_id = 1;
                    if (language >= 0 && translation->language == (uint32_t)language)
                        expected = translation->data;
                }
            }
            translation_probes = language_queries = 0;
            assert(cfw_translated_string(id) == expected);
            assert(translation_probes <= 3);
            assert(language_queries == (unsigned)known_id);
        }
        translation_probes = language_queries = 0;
        assert(!cfw_translated_string(UINT32_MAX));
        assert(translation_probes <= 3 && !language_queries);
    }
    language = -1;
    char formatted[64];
    assert(!strcmp(cfw_string(999), ""));
    assert(cfw_format(formatted, sizeof(formatted), 200, "value %u", (uint32_t)42));
    assert(!strcmp(formatted, "value %u: 42%"));
    language = 4;
    cfw_format(formatted, sizeof(formatted), 200, "Song", (uint32_t)3);
    assert(!strcmp(formatted, "3% - Song Song"));
    language = 20;
    cfw_format(formatted, sizeof(formatted), 200, "Song", (uint32_t)0);
    assert(!strcmp(formatted, "Song: 0%"));
    cfw_format(formatted, sizeof(formatted), 202, UINT32_MAX);
    assert(!strcmp(formatted, "4294967295"));
    cfw_format(formatted, sizeof(formatted), 203);
    assert(!strcmp(formatted, "100%!"));
    cfw_format(formatted, sizeof(formatted), 201, (const char *)NULL);
    assert(!*formatted);
    assert(!cfw_format(NULL, 0, 201, "ignored"));
    cfw_format(formatted, 1, 201, "x");
    assert(!*formatted);
    cfw_format(formatted, 3, 201, "A\xc3\xa4Z");
    assert(!strcmp(formatted, "A"));
    cfw_format(formatted, 4, 201, "A\xc3\xa4Z");
    assert(!strcmp(formatted, "A\xc3\xa4"));
    cfw_format(formatted, sizeof(formatted), 999);
    assert(!*formatted);
    language = -1;
    assert(!cfw_translated_string(100));
    language = 4;
    assert(!strcmp(cfw_translated_string(100), "Deutsch"));
    assert(!cfw_translated_string(999));
    language = 0;
    assert(!cfw_translated_string(100));
    assert(!strcmp(cfw_translated_string(101), "English"));
    language = 2;
    assert(!strcmp(cfw_translated_string(100), "Francais"));
    language = 20;
    assert(!cfw_translated_string(100));
    language = 4;
    assert(!strcmp(cfw_resource_service_get(NULL, 0x53747220, 100), "Deutsch"));
    assert(!resource_lookups);
    language = 20;
    assert(!strcmp(cfw_resource_service_get(NULL, 0x53747220, 100), "Stock"));
    assert(resource_lookups == 1);
    assert(!strcmp(cfw_resource_service_get(NULL, OSOS_RESOURCE_BMAP, 100), "Stock"));
    assert(resource_lookups == 2);
    cfw_install_resources(&bank);
    assert(installed == 5 && name_id == ALBUM_ARTISTS);
    assert(cfw_resource_bitmap(26) == icon && !cfw_resource_bitmap(99));
    registry[ARTISTS]->visible = 1;
    registry[ALBUM_ARTISTS]->visible = 1;
    registry[ITUNES_U]->visible = 0;

    for (unsigned entry = 0; entry < 20; entry++) {
        struct list main, music;
        load(&main, 26);
        load(&music, 27);
        assert(main.count == 5 && music.count == 4);
        assert(main.blocks[0]->data == &stock[0]);
        assert(main.blocks[1]->data == &added[0] && main.blocks[1]->metadata == 7);
        assert(main.blocks[2]->data == &added[1] && main.blocks[2]->metadata == 8);
        assert(main.blocks[3]->data == &stock[1]);
        assert(music.blocks[1]->data == &added[2]);
        assert(((struct row *)main.blocks[0]->data)->visible == 1);
        assert(((struct row *)main.blocks[1]->data)->visible == 1);
        assert(((struct row *)main.blocks[3]->data)->visible == 0);
        destroy(&music);
        destroy(&main);
        assert(!allocations);
    }
    struct list unrelated;
    load(&unrelated, 28);
    assert(unrelated.count == 3);
    destroy(&unrelated);
    unrelated.selectors[2] = 0x5349454d;
    unrelated.selectors[3] = 26;
    cfw_ui_append_resource_block(&unrelated, &stock[0], sizeof(stock[0]), 0);
    assert(unrelated.count == 1);
    destroy(&unrelated);
    unrelated.selectors[2] = 0x4954454d;
    cfw_ui_append_resource_block(&unrelated, &stock[0], 4, 0);
    assert(unrelated.count == 1);
    destroy(&unrelated);
    assert(!allocations);
    return 0;
}
