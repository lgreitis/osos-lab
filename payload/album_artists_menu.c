/* SPDX-License-Identifier: GPL-3.0-only */
#include "osos.h"
#include "album_artists_menu.h"
#include "album_artists_ui.h"
#include "patch.h"
#include "preferences.h"

static uint32_t visible = 2;
static int loaded;
static const struct osos_path settings_path = {0, "iPod_Control\\Device\\CFWMenu"};
static const uint32_t menu_items[] = {CFW_AlbumArtists_MainItem, CFW_AlbumArtists_MusicItem};
static const uint32_t settings_items[] = {OSOS_SETTINGS_MAIN_MENU_ITEM, OSOS_SETTINGS_MUSIC_MENU_ITEM};

void *cfw_main_menu_reset_original(void *model);
void *cfw_music_menu_reset_original(void *model);

static void load_preferences(void)
{
    if (loaded)
        return;
    loaded = 1;
    struct album_artists_menu_file file;
    if (cfw_preferences_read(&settings_path, &file, sizeof(file)) &&
        album_artists_menu_valid(&file))
        visible = file.visible;
}

static void save_preferences(void)
{
    struct album_artists_menu_file file = album_artists_menu_encode(visible);
    cfw_preferences_write(&settings_path, &file, sizeof(file));
}

static void apply_visibility(unsigned menu)
{
    ((int (*)(uint32_t, uint32_t))OSOS_MENU_ITEM_SET_VISIBLE)(
        menu_items[menu], (visible >> menu) & 1);
}

static uint32_t selected_index(void *selection)
{
    return ((uint32_t (*)(void *))osos_method(selection, 0x168))(selection);
}

void cfw_album_artists_menu_adjust_property(uint32_t type, uint32_t property,
                                            uintptr_t *out)
{
    if (type == OSOS_PROPERTY_COUNT && (property == 0x8909 || property == 0x890a))
        ++*out;
}

PATCH_ARM int cfw_album_artists_menu_item(void *model, uint32_t type,
    uint32_t property, uint32_t index, uintptr_t *out)
{
    typedef int (*native)(void *, uint32_t, uint32_t, uint32_t, uintptr_t *);
    native get = (native)OSOS_SETTINGS_INDEXED_PROPERTY;
    if (property != 0x8909 && property != 0x890a)
        return get(model, type, property, index, out);
    unsigned menu = property == 0x890a;
    uint32_t count = menu ? 14 : OSOS_MAIN_MENU_SETTINGS_COUNT + 1;
    if (index >= count)
        return 0;
    uint32_t original = album_artists_menu_index(menu, index);
    int custom = original == UINT32_MAX;
    int result = get(model, type, property, custom ? index - 1 : original, out);
    if (!result)
        return result;
    if (custom && type == OSOS_PROPERTY_STRING) {
        *out = osos_ui_string(CFW_AlbumArtists_Title);
    } else if (type == 0x424d6170) {
        const uint8_t *rows = (const uint8_t *)(menu ? OSOS_MUSIC_MENU_SETTINGS_ROWS :
                                                    OSOS_MAIN_MENU_SETTINGS_ROWS);
        int checked = custom ? (visible >> menu) & 1 : rows[original * 20 + 16] != 0;
        void *selection = ((void *(*)(void))OSOS_UI_SELECTION)();
        int selected = index == selected_index(selection);
        uint32_t icon = checked ? OSOS_MENU_CHECKED_BITMAP + selected :
                        selected && index == count - 1 ? OSOS_MENU_RESET_BITMAP : 0;
        *out = icon ? osos_ui_bitmap(icon) : 0;
    }
    return result;
}

PATCH_ARM uint32_t cfw_album_artists_main_menu_index(void *selection)
{
    return album_artists_menu_index(ALBUM_ARTISTS_MAIN_MENU, selected_index(selection));
}

PATCH_ARM uint32_t cfw_album_artists_music_menu_index(void *selection)
{
    return album_artists_menu_index(ALBUM_ARTISTS_MUSIC_MENU, selected_index(selection));
}

static void toggle(void *model, unsigned menu)
{
    load_preferences();
    visible ^= 1u << menu;
    save_preferences();
    apply_visibility(menu);
    osos_model_notify(model, OSOS_NOTIFY_ALL, settings_items[menu]);
}

PATCH_ARM void cfw_album_artists_main_menu_toggle(void *model, uint32_t index)
{
    if (index == UINT32_MAX)
        toggle(model, ALBUM_ARTISTS_MAIN_MENU);
    else
        ((void (*)(void *, uint32_t))OSOS_MAIN_MENU_TOGGLE)(model, index);
}

PATCH_ARM void cfw_album_artists_music_menu_toggle(void *model, uint32_t index)
{
    if (index == UINT32_MAX)
        toggle(model, ALBUM_ARTISTS_MUSIC_MENU);
    else
        ((void (*)(void *, uint32_t))OSOS_MUSIC_MENU_TOGGLE)(model, index);
}

PATCH_ARM void *cfw_album_artists_menu_load(void *model)
{
    void *result = ((void *(*)(void *))OSOS_MUSIC_MENU_APPLY)(model);
    load_preferences();
    apply_visibility(ALBUM_ARTISTS_MAIN_MENU);
    apply_visibility(ALBUM_ARTISTS_MUSIC_MENU);
    return result;
}

static void reset(unsigned menu)
{
    load_preferences();
    uint32_t mask = 1u << menu;
    visible = (visible & ~mask) | (menu == ALBUM_ARTISTS_MUSIC_MENU ? mask : 0);
    save_preferences();
    apply_visibility(menu);
}

PATCH_ARM void *cfw_album_artists_main_menu_reset(void *model)
{
    reset(ALBUM_ARTISTS_MAIN_MENU);
    return cfw_main_menu_reset_original(model);
}

PATCH_ARM void *cfw_album_artists_music_menu_reset(void *model)
{
    reset(ALBUM_ARTISTS_MUSIC_MENU);
    return cfw_music_menu_reset_original(model);
}
