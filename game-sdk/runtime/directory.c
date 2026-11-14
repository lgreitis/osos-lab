/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "native.h"

struct game_directory {
    struct osos_directory_iterator iterator;
    int active;
    unsigned root_length;
};
static struct game_directory directories[4];

struct game_directory *game_directory_open(const char *name, int recursive)
{
    struct osos_path path;
    if (game_make_path(&path, name, 1) < 0)
        return NULL;
    for (unsigned i = 0; i < 4; ++i) {
        struct game_directory *directory = &directories[i];
        if (!directory->active) {
            osos_directory_init(&directory->iterator, path.text, recursive);
            directory->root_length = 0;
            while (path.text[directory->root_length])
                ++directory->root_length;
            directory->active = 1;
            return directory;
        }
    }
    return NULL;
}

int game_directory_read(struct game_directory *directory,
                        struct game_directory_entry *entry)
{
    if (!directory || !directory->active || !entry)
        return -1;
    struct osos_native_path_string path;
    osos_path_string_init(&path);
    entry->name[0] = 0;
    entry->is_directory = 0;
    int result = osos_directory_next(&directory->iterator, &path, &entry->is_directory);
    if (result) {
        const char *name = osos_path_string_text(&path);
        unsigned i = 0;
        while (name[i] && i + 1 < sizeof(entry->name)) {
            entry->name[i] = name[i];
            ++i;
        }
        entry->name[i] = 0;
        result = name[i] || directory->root_length + 1 + i >=
                                sizeof(((struct osos_path *)0)->text)
                     ? -1
                     : 1;
    }
    osos_path_string_destroy(&path);
    return result;
}

void game_directory_close(struct game_directory *directory)
{
    if (directory && directory->active) {
        osos_directory_destroy(&directory->iterator);
        directory->active = 0;
    }
}

void game_directories_close_all(void)
{
    for (unsigned i = 0; i < 4; ++i)
        game_directory_close(&directories[i]);
}
