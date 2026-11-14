/* SPDX-License-Identifier: GPL-3.0-only */
#include "game_api.h"
#include "platform.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

const struct game_app game_application = {.package_id = "test-game"};
static unsigned opened, closed, strings, native_opens;
static char opened_path[258], long_name[300];
static const char *result_name;

void osos_directory_init(struct osos_directory_iterator *iterator, const char *path,
                         int recursive)
{
    assert(recursive == 1);
    strcpy(opened_path, path);
    iterator->state[0] = 0;
    ++opened;
}

int osos_directory_next(struct osos_directory_iterator *iterator,
                        struct osos_native_path_string *path, uint8_t *is_directory)
{
    (void)path;
    unsigned index = iterator->state[0]++;
    *is_directory = index == 1;
    result_name = index == 0 ? "roms/Example.NES" : index == 1 ? "roms" : long_name;
    return index < 3;
}

void osos_directory_destroy(struct osos_directory_iterator *iterator)
{
    (void)iterator;
    ++closed;
}

void osos_path_string_init(struct osos_native_path_string *path)
{
    (void)path;
    ++strings;
}

const char *osos_path_string_text(struct osos_native_path_string *path)
{
    (void)path;
    return result_name;
}

void osos_path_string_destroy(struct osos_native_path_string *path)
{
    (void)path;
    --strings;
}

int osos_file_open(const struct osos_path *path, void **handle)
{
    assert(path->volume == 0);
    strcpy(opened_path, path->text);
    *handle = &native_opens;
    ++native_opens;
    return 0;
}

int osos_file_size(void *handle, uint32_t *size)
{
    (void)handle;
    *size = 0;
    return 0;
}

int osos_file_read(void *handle, void *buffer, uint32_t size, uint32_t *read)
{
    (void)handle;
    (void)buffer;
    (void)size;
    *read = 0;
    return 0;
}

int osos_file_seek(void *handle, int64_t offset)
{
    (void)handle;
    (void)offset;
    return 0;
}

void osos_file_close(void *handle)
{
    (void)handle;
    --native_opens;
}

int main(void)
{
    const char *invalid[] = {NULL,   "/root", "../other", "roms/../../other",
                             "a//b", "roms/", "a/./b",    "a\\b",
                             "a:b"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!game_directory_open(invalid[i], 1));
        assert(!game_file_open(invalid[i]));
    }
    assert(!opened && !native_opens);
    struct game_directory *dirs[4];
    for (unsigned i = 0; i < 4; ++i)
        assert((dirs[i] = game_directory_open("", 1)));
    assert(!strcmp(opened_path, "iPod_Control/games_RO/test-game"));
    assert(!game_directory_open("", 1));
    struct game_directory_entry entry;
    assert(game_directory_read(dirs[0], &entry) == 1);
    assert(!strcmp(entry.name, "roms/Example.NES") && !entry.is_directory);
    assert(game_directory_read(dirs[0], &entry) == 1 && entry.is_directory);
    memset(long_name, 'x', sizeof(long_name) - 1);
    assert(game_directory_read(dirs[0], &entry) == -1 && !strings);
    assert(game_directory_read(dirs[0], &entry) == 0 && !entry.name[0] && !strings);
    game_directory_close(dirs[0]);
    game_directory_close(dirs[0]);
    assert(closed == 1);
    assert(game_directory_read(dirs[0], &entry) == -1);
    assert((dirs[0] = game_directory_open("./roms", 1)));
    assert(!strcmp(opened_path, "iPod_Control/games_RO/test-game/roms"));
    game_directories_close_all();
    assert(opened == closed && !strings);
    assert(!game_file_open(""));
    struct game_file *file = game_file_open("./roms/Example.NES");
    assert(file &&
           !strcmp(opened_path, "iPod_Control/games_RO/test-game/roms/Example.NES"));
    game_files_close_all();
    assert(!native_opens);
    struct osos_path path;
    char name[258] = {0};
    unsigned prefix = strlen("iPod_Control/games_RO/test-game/");
    memset(name, 'x', 257 - prefix);
    assert(game_make_path(&path, name, 0) == 0 && strlen(path.text) == 257);
    name[257 - prefix] = 'x';
    assert(game_make_path(&path, name, 0) == -1);
    assert(!game_directory_open(name, 1));
    puts("SDK path boundaries, directory lifecycle and entry-copy checks passed");
}
