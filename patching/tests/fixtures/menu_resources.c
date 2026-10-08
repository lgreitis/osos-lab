/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM
#define OSOS_MENU_ITEMS_REGISTER_TYPE register_items
struct osos_string { const char *data; };
static void register_items(uint32_t);
static void osos_resource_set(void *, uint32_t, uint32_t, const void *, uint32_t);
static const void *osos_resource_next(void *bank, uint32_t *id, uint32_t *size)
{
    (void)bank; (void)id; (void)size;
    return NULL;
}
static void osos_string_init(struct osos_string *s, const char *text) { s->data = text; }
static void osos_string_destroy(struct osos_string *s) { (void)s; }
static uint32_t name_id;
static uint32_t *osos_resource_name_slot(const struct osos_string *s)
{
    (void)s;
    return &name_id;
}
#include "../../../payload/resources.c"

enum { ARTISTS = 1, ITUNES_U, NOW_PLAYING, ALBUM_ARTISTS };
struct row { uint32_t id, visible; };
static struct row main_stock[] = {{ARTISTS, 0}, {ITUNES_U, 1}, {NOW_PLAYING, 0}};
static struct row music_stock[] = {{ARTISTS, 1}, {ITUNES_U, 1}};
static const struct row main_copy[] = {
    {ARTISTS, 0}, {ALBUM_ARTISTS, 0}, {ITUNES_U, 1}, {NOW_PLAYING, 0}
};
static const struct row music_copy[] = {{ARTISTS, 1}, {ALBUM_ARTISTS, 1}, {ITUNES_U, 1}};
static struct row main_override[4], music_override[3];
static struct row *main_registry[5], *music_registry[5];
static unsigned installed, registered;
static int bank;
const struct cfw_resource cfw_resources[] = {
    {0x4954454d, 26, sizeof(main_copy), (const unsigned char *)main_copy},
    {0x4954454d, 27, sizeof(music_copy), (const unsigned char *)music_copy},
};
const uint32_t cfw_resource_count = 2;
const struct cfw_resource_name cfw_resource_names[] = {{"AlbumArtists", ALBUM_ARTISTS}};
const uint32_t cfw_resource_name_count = 1;

static void osos_resource_set(void *target, uint32_t type, uint32_t id,
                              const void *data, uint32_t size)
{
    assert(target == &bank && type == 0x4954454d && (id == 26 || id == 27));
    assert(size == (id == 26 ? sizeof(main_copy) : sizeof(music_copy)));
    memcpy(id == 26 ? main_override : music_override, data, size);
    installed++;
}

static void register_items(uint32_t type)
{
    assert(type == 0x4954454d && installed == cfw_resource_count);
    for (unsigned i = 0; i < 4; i++)
        main_registry[main_override[i].id] = &main_override[i];
    for (unsigned i = 0; i < 3; i++)
        music_registry[music_override[i].id] = &music_override[i];
    registered++;
}

int main(void)
{
    for (unsigned i = 0; i < 3; i++)
        main_registry[main_stock[i].id] = &main_stock[i];
    for (unsigned i = 0; i < 2; i++)
        music_registry[music_stock[i].id] = &music_stock[i];
    cfw_install_resources(&bank);
    assert(registered == 1 && name_id == ALBUM_ARTISTS);

    /* Settings and playback must update the same rows that the menus display. */
    main_registry[ARTISTS]->visible = 1;
    main_registry[ITUNES_U]->visible = 0;
    main_registry[NOW_PLAYING]->visible = 1;
    main_registry[ALBUM_ARTISTS]->visible = 1;
    music_registry[ITUNES_U]->visible = 0;
    music_registry[ALBUM_ARTISTS]->visible = 0;
    assert(main_override[0].visible == 1 && main_override[1].visible == 1);
    assert(main_override[2].visible == 0 && main_override[3].visible == 1);
    assert(music_override[1].visible == 0 && music_override[2].visible == 0);
    assert(main_stock[0].visible == 0 && main_stock[1].visible == 1);
    assert(main_stock[2].visible == 0 && music_stock[1].visible == 1);
    return 0;
}
