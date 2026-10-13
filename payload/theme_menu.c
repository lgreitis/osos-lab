/* SPDX-License-Identifier: GPL-3.0-only */

#include "theme_menu.h"
#include "assets.h"
#include "cfw.h"
#include "theme_menu_ui.h"
#include "osos.h"
#include "preferences.h"

enum { THEME_LIMIT = CFW_THEME_CAPACITY - 1, THEME_RECORD = 0x314d4854 };

struct theme_entry {
    struct osos_path path;
    char name[48];
};

struct theme_preference {
    uint32_t magic;
    char path[258];
};
static struct theme_entry entries[THEME_LIMIT];
static unsigned entry_count;
static char selected[258];
static int failed = -1;
static const struct osos_path preference_path = {0, "iPod_Control\\Device\\CFWTheme"};

static unsigned length(const char *text, unsigned capacity)
{
    unsigned size = 0;
    while (size < capacity && text[size])
        size++;
    return size;
}

static int copy_text(char *out, unsigned capacity, const char *text)
{
    unsigned size = length(text, capacity);
    if (size == capacity)
        return 0;

    for (unsigned i = 0; i <= size; i++)
        out[i] = text[i];
    return 1;
}

static int valid_path(const char *path)
{
    const char *prefix = "themes\\";
    if (length(path, sizeof(selected)) == sizeof(selected))
        return 0;

    for (unsigned i = 0; prefix[i]; i++)
        if (path[i] != prefix[i])
            return 0;

    for (unsigned i = 0; path[i]; i++)
        if ((unsigned char)path[i] < 32 || path[i] == '/' || path[i] == ':' ||
            (path[i] == '.' && path[i + 1] == '.'))
            return 0;
    return path[7] != 0;
}

void cfw_theme_load(void)
{
    struct theme_preference preference;
    cfw_zero_bytes(&preference, sizeof(preference));
    selected[0] = 0;
    cfw_assets_set_enabled(0);

    if (cfw_preferences_read(&preference_path, &preference, sizeof(preference)) &&
        preference.magic == THEME_RECORD) {
        if (!preference.path[0] || !valid_path(preference.path))
            return;

        struct osos_path path;
        cfw_zero_bytes(&path, sizeof(path));
        copy_text(path.text, sizeof(path.text), preference.path);
        if (cfw_assets_prepare_path(&path)) {
            cfw_assets_set_enabled(1);
            copy_text(selected, sizeof(selected), path.text);
        }
    } else {
        static const struct osos_path legacy = {0, "iPod_Control\\Device\\CFWDark"};
        uint32_t record;
        if (cfw_preferences_read(&legacy, &record, sizeof(record)) &&
            record == 0x314b5244 && cfw_assets_prepare()) {
            cfw_assets_set_enabled(1);
            copy_text(selected, sizeof(selected), "themes\\dark\\theme.bin");
        }
    }
}

static int compare(const char *left, const char *right)
{
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

static int theme_file(const char *name, unsigned size)
{
    const char *extensions[] = {".bin", ".theme"};
    for (unsigned i = 0; i < 2; i++) {
        unsigned n = length(extensions[i], 8);
        if (size <= n)
            continue;

        unsigned j = 0;
        for (; j < n; j++) {
            char c = name[size - n + j];
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
            if (c != extensions[i][j])
                break;
        }
        if (j == n)
            return 1;
    }
    return 0;
}

static void scan(void)
{
    entry_count = 0;
    struct osos_directory directory;
    struct osos_native_string name;
    osos_native_string_init(&name);
    osos_directory_init(&directory, "themes");

    unsigned char is_directory;
    while (entry_count < THEME_LIMIT &&
           osos_directory_next(&directory, &name, &is_directory)) {
        const char *text = osos_native_string_data(&name);
        if (!text || !text[0] || text[0] == '.')
            continue;

        unsigned size = length(text, 240);
        if (size == 240 || (!is_directory && !theme_file(text, size)))
            continue;

        struct theme_entry *entry = &entries[entry_count];
        cfw_zero_bytes(entry, sizeof(*entry));
        copy_text(entry->path.text, sizeof(entry->path.text), "themes\\");
        copy_text(entry->path.text + 7, sizeof(entry->path.text) - 7, text);
        if (is_directory)
            copy_text(entry->path.text + 7 + size, sizeof(entry->path.text) - 7 - size,
                      "\\theme.bin");
        if (!valid_path(entry->path.text))
            continue;

        struct cfw_theme_metadata metadata;
        if (!cfw_assets_metadata(&entry->path, &metadata))
            continue;

        if (metadata.name[0]) {
            copy_text(entry->name, sizeof(entry->name), metadata.name);
        } else {
            unsigned n = size < 47 ? size : 47;
            while (n && ((unsigned char)text[n] & 0xc0) == 0x80)
                n--;
            for (unsigned i = 0; i < n; i++)
                entry->name[i] = text[i];
            entry->name[n] = 0;
        }
        entry_count++;
    }

    osos_directory_destroy(&directory);
    osos_native_string_destroy(&name);

    for (unsigned i = 1; i < entry_count; i++) {
        struct theme_entry value;
        cfw_copy_bytes(&value, &entries[i], sizeof(value));
        unsigned j = i;
        while (j && compare(entries[j - 1].name, value.name) > 0) {
            cfw_copy_bytes(&entries[j], &entries[j - 1], sizeof(value));
            j--;
        }
        cfw_copy_bytes(&entries[j], &value, sizeof(value));
    }
}

static void refresh(void)
{
    for (unsigned i = 0; i <= entry_count; i++) {
        char label[64];
        copy_text(label, sizeof(label), i ? entries[i - 1].name : "Default");

        int checked =
            i ? cfw_string_equal(selected, entries[i - 1].path.text) : !selected[0];
        unsigned size = length(label, sizeof(label));
        copy_text(label + size, sizeof(label) - size,
                  failed == (int)i ? " (failed)"
                  : checked        ? " *"
                                   : "");
        cfw_ui_list_label(&cfw_theme, i, label);
    }
}

static void choose(void *controller, unsigned index)
{
    const char *path = index ? entries[index - 1].path.text : "";
    failed = -1;
    if (cfw_string_equal(selected, path)) {
        refresh();
        return;
    }

    if (index && !cfw_assets_prepare_path(&entries[index - 1].path)) {
        failed = index;
        refresh();
        return;
    }

    struct theme_preference preference;
    cfw_zero_bytes(&preference, sizeof(preference));
    preference.magic = THEME_RECORD;
    copy_text(preference.path, sizeof(preference.path), path);
    if (!cfw_preferences_write(&preference_path, &preference, sizeof(preference))) {
        cfw_assets_cancel();
        failed = index;
        refresh();
        return;
    }

    cfw_assets_set_enabled(index != 0);
    copy_text(selected, sizeof(selected), path);
    refresh();

    osos_model_notify(osos_settings_model(controller), OSOS_NOTIFY_ALL, 0);
    void *now_playing = osos_now_playing_model();
    if (now_playing) {
        osos_model_notify(now_playing, OSOS_RESOURCE_DRAW, 0x7f0c);
        osos_model_notify(now_playing, OSOS_RESOURCE_DRAW, 0x7f0d);
    }
    osos_ui_invalidate();
}

int cfw_theme_action(void *controller, const char *action, uint32_t argument)
{
    if (cfw_string_equal(action, "CFW_Theme_Open")) {
        if (argument != OSOS_ACTION_SUPPORT_QUERY) {
            scan();
            if (!cfw_ui_list_count(&cfw_theme, entry_count + 1))
                return 1;
            failed = -1;
            refresh();
        }
        return 1;
    }

    const char *prefix = "CFW_Theme_";
    unsigned n = 0;
    if (!action)
        return 0;

    while (prefix[n] && prefix[n] == action[n])
        n++;
    if (prefix[n] || length(action + n, 3) != 2 || action[n] < '0' || action[n] > '9' ||
        action[n + 1] < '0' || action[n + 1] > '9')
        return 0;

    unsigned index = (action[n] - '0') * 10 + action[n + 1] - '0';
    if (index > THEME_LIMIT)
        return 0;
    if (argument != OSOS_ACTION_SUPPORT_QUERY && index <= entry_count)
        choose(controller, index);
    return 1;
}
