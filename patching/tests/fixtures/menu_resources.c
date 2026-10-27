/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM
#define OSOS_RESOURCE_BLOCK_APPEND append_block
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

static uint32_t *osos_resource_name_slot(const struct osos_string *s)
{
    (void)s;
    return &name_id;
}

#include "../../../payload/resources.c"

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
};
const uint32_t cfw_resource_count = 2;
const struct cfw_resource_name cfw_resource_names[] = {{"AlbumArtists", ALBUM_ARTISTS}};
const uint32_t cfw_resource_name_count = 1;
static unsigned installed;
static int bank;

static void osos_resource_set(void *target, uint32_t type, uint32_t id,
                              const void *data, uint32_t size)
{
    assert(target == &bank && type == 0x53747220 && id == 99);
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
    cfw_install_resources(&bank);
    assert(installed == 1 && name_id == ALBUM_ARTISTS);
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
