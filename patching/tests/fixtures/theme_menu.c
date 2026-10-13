/* SPDX-License-Identifier: GPL-3.0-only */

#include <assert.h>
#include <string.h>
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert
#include "../../../payload/assets.h"
#include "../../../payload/ui.h"

const struct cfw_ui_list cfw_theme = {0, 17, NULL, NULL};
static unsigned char preference[264];
static int has_preference, legacy_on, legacy_handle, fail_save, missing_pack;
static int scans, destroyed, prepared, active, cancelled, notifications, redraws;
static unsigned cursor, current_file;
static unsigned list_count;
static char labels[17][64], loaded_path[258];
static int controller, settings_model, playback_model;

struct file {
    const char *name, *title;
    int directory, valid;
};
static const struct file files[] = {{"purple.bin", "Purple", 0, 1},
                                    {".hidden", "Hidden", 1, 1},
                                    {"dark", "Dark", 1, 1},
                                    {"bad.bin", "Broken", 0, 0},
                                    {"nameless.theme", "", 0, 1},
                                    {"readme.txt", "Readme", 0, 1},
                                    {"../outside.bin", "Bad path", 0, 1}};

static void directory_init(struct osos_directory *directory, const char *path)
{
    (void)directory;
    assert(!strcmp(path, "themes"));
    cursor = 0;
    scans++;
}

static int directory_next(struct osos_directory *directory,
                          struct osos_native_string *name, unsigned char *folder)
{
    (void)directory;
    if (cursor == sizeof(files) / sizeof(files[0]))
        return 0;
    current_file = cursor++;
    name->data = (char *)files[current_file].name;
    *folder = files[current_file].directory;
    return 1;
}

static void directory_destroy(struct osos_directory *directory)
{
    (void)directory;
    destroyed++;
}

static void native_string_init(struct osos_native_string *name)
{
    name->data = NULL;
}

static void native_string_destroy(struct osos_native_string *name)
{
    (void)name;
}

static const char *native_string_data(struct osos_native_string *name)
{
    return name->data;
}

int cfw_assets_metadata(const struct osos_path *path,
                        struct cfw_theme_metadata *metadata)
{
    assert(!strncmp(path->text, "themes\\", 7));
    if (files[current_file].directory)
        assert(strstr(path->text, "\\theme.bin"));
    memset(metadata, 0, sizeof(*metadata));
    strcpy(metadata->name, files[current_file].title);
    return files[current_file].valid;
}

int cfw_assets_prepare_path(const struct osos_path *path)
{
    prepared++;
    if (missing_pack)
        return 0;
    strcpy(loaded_path, path->text);
    return 1;
}

int cfw_assets_prepare(void)
{
    static const struct osos_path path = {0, "themes\\dark\\theme.bin"};
    return cfw_assets_prepare_path(&path);
}

void cfw_assets_set_enabled(int enabled)
{
    active = enabled;
}

void cfw_assets_cancel(void)
{
    cancelled++;
}

int cfw_ui_list_count(const struct cfw_ui_list *list, unsigned count)
{
    assert(list == &cfw_theme && count <= 17);
    list_count = count;
    return 1;
}

void cfw_ui_list_label(const struct cfw_ui_list *list, unsigned index,
                       const char *label)
{
    assert(list == &cfw_theme && index < list_count && strlen(label) < 64);
    strcpy(labels[index], label);
}

static int file_open(const struct osos_path *path, int mode, void **handle)
{
    if (!strcmp(path->text, "iPod_Control\\Device\\CFWDark")) {
        if (!legacy_on)
            return -1;
        *handle = &legacy_handle;
    } else {
        assert(!strcmp(path->text, "iPod_Control\\Device\\CFWTheme"));
        if (mode == 1 && !has_preference)
            return -1;
        *handle = preference;
    }
    return 0;
}

static int file_read(void *handle, void *out, uint32_t size, uint32_t *count)
{
    if (handle == &legacy_handle) {
        assert(size == 4);
        *(uint32_t *)out = 0x314b5244;
    } else {
        assert(handle == preference && size == sizeof(preference));
        memcpy(out, preference, size);
    }
    *count = size;
    return 0;
}

static int file_write(void *handle, const void *in, uint32_t size, uint32_t *count)
{
    assert(handle == preference && size == sizeof(preference));
    *count = fail_save ? 0 : size;
    if (!fail_save) {
        memcpy(preference, in, size);
        has_preference = 1;
    }
    return 0;
}

static void file_close(void *handle)
{
    (void)handle;
}

static void *model(void *owner)
{
    assert(owner == &controller);
    return &settings_model;
}

static void *playback_model_get(void)
{
    return &playback_model;
}

static void notify(void *owner, uint32_t type, uint32_t property)
{
    if (owner == &playback_model)
        assert(type == OSOS_RESOURCE_DRAW &&
               (property == 0x7f0c || property == 0x7f0d));
    else
        assert(owner == &settings_model && type == OSOS_NOTIFY_ALL);
    notifications++;
}

static void invalidate(void)
{
    redraws++;
}

#define osos_directory_init directory_init
#define osos_directory_next directory_next
#define osos_directory_destroy directory_destroy
#define osos_native_string_init native_string_init
#define osos_native_string_destroy native_string_destroy
#define osos_native_string_data native_string_data
#define osos_file_open file_open
#define osos_file_read file_read
#define osos_file_write file_write
#define osos_file_close file_close
#define osos_settings_model model
#define osos_now_playing_model playback_model_get
#define osos_model_notify notify
#define osos_ui_invalidate invalidate
#include "../../../payload/theme_menu.c"

int main(void)
{
    cfw_theme_load();
    assert(!active && !prepared && !scans);
    assert(cfw_theme_action(&controller, "CFW_Theme_Open", OSOS_ACTION_SUPPORT_QUERY));
    assert(!scans);

    assert(cfw_theme_action(&controller, "CFW_Theme_Open", 0));
    assert(scans == 1 && destroyed == 1 && entry_count == 3);
    assert(!strcmp(labels[0], "Default *"));
    assert(!strcmp(labels[1], "Dark") && !strcmp(labels[2], "Purple"));
    assert(!strcmp(labels[3], "nameless.theme") && list_count == 4);

    assert(!cfw_theme_action(&controller, "stock", 0));
    assert(!cfw_theme_action(&controller, "CFW_Theme_1", 0));
    assert(!cfw_theme_action(&controller, "CFW_Theme_17", 0));
    assert(cfw_theme_action(&controller, "CFW_Theme_02", OSOS_ACTION_SUPPORT_QUERY));
    assert(!prepared);

    assert(cfw_theme_action(&controller, "CFW_Theme_02", 0));
    assert(active && !strcmp(selected, "themes\\purple.bin"));
    assert(!strcmp(labels[2], "Purple *") && notifications == 3 && redraws == 1);

    missing_pack = 1;
    assert(cfw_theme_action(&controller, "CFW_Theme_01", 0));
    assert(active && !strcmp(selected, "themes\\purple.bin") && redraws == 1);
    assert(!strcmp(labels[1], "Dark (failed)"));

    missing_pack = 0;
    fail_save = 1;
    assert(cfw_theme_action(&controller, "CFW_Theme_01", 0));
    assert(active && !strcmp(selected, "themes\\purple.bin") && cancelled == 1);

    fail_save = 0;
    cfw_theme_load();
    assert(active && !strcmp(selected, "themes\\purple.bin") && scans == 1);
    assert(cfw_theme_action(&controller, "CFW_Theme_01", 0));
    assert(active && !strcmp(selected, "themes\\dark\\theme.bin"));

    missing_pack = 1;
    assert(cfw_theme_action(&controller, "CFW_Theme_00", 0));
    assert(!active && !selected[0] && !strcmp(labels[0], "Default *"));

    legacy_on = 1;
    int prior_prepared = prepared;
    cfw_theme_load();
    assert(!active && prepared == prior_prepared);

    has_preference = 0;

    missing_pack = 0;
    cfw_theme_load();
    assert(active && !strcmp(selected, "themes\\dark\\theme.bin"));

    has_preference = 1;
    struct theme_preference *saved = (void *)preference;
    saved->magic = THEME_RECORD;
    memset(saved->path, 'x', sizeof(saved->path));
    cfw_theme_load();
    assert(!active && !selected[0]);
}
