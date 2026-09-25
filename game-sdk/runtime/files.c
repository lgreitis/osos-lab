#include "platform.h"
#include "native.h"

#define PACKAGE_ROOT "iPod_Control/games_RO/"

struct game_file {
    void *handle;
    uint32_t size;
    uint32_t position;
};

static struct game_file files[12];

struct game_file *game_file_open(const char *name)
{
    struct osos_path path = {0, PACKAGE_ROOT};

    unsigned i = sizeof(PACKAGE_ROOT) - 1;
    const char *id = game_application.package_id;
    if (!id || !*id || *id == '.')
        return NULL;

    for (; *id; ++id) {
        if (*id == '/' || *id == '\\' || *id == ':' || i >= sizeof(path.text) - 2)
            return NULL;
        path.text[i++] = *id;
    }
    path.text[i++] = '/';

    if (!name || !*name)
        return NULL;
    while (name[0] == '.' && name[1] == '/')
        name += 2;
    if (!*name || *name == '/')
        return NULL;

    const char *component = name;
    for (; *name; ++name) {
        if (name == component &&
            (*name == '.' && (name[1] == '/' || name[1] == 0 ||
                              (name[1] == '.' && (name[2] == '/' || name[2] == 0)))))
            return NULL;
        if (*name == '\\' || *name == ':' || i >= sizeof(path.text) - 1)
            return NULL;
        if (*name == '/') {
            if (name == component || !name[1])
                return NULL;
            component = name + 1;
        }
        path.text[i++] = *name;
    }
    path.text[i] = 0;

    for (unsigned slot = 0; slot < sizeof(files) / sizeof(files[0]); ++slot) {
        struct game_file *file = &files[slot];
        if (file->handle)
            continue;

        if (osos_file_open(&path, &file->handle) != 0) {
            file->handle = NULL;
            return NULL;
        }
        if (osos_file_size(file->handle, &file->size) != 0) {
            game_file_close(file);
            return NULL;
        }
        file->position = 0;
        return file;
    }

    return NULL;
}

int game_file_read(struct game_file *file, void *buffer, size_t size)
{
    uint32_t read = 0;

    if (!file || !file->handle || (!buffer && size))
        return -1;
    if (size > file->size - file->position)
        size = file->size - file->position;
    if (!size)
        return 0;

    int result = osos_file_read(file->handle, buffer, size, &read);
    if (read > size)
        return -1;
    file->position += read;
    return read ? (int)read : (result ? -1 : 0);
}

int game_file_seek(struct game_file *file, uint32_t offset)
{
    if (!file || !file->handle || offset > file->size ||
        osos_file_seek(file->handle, offset) != 0)
        return -1;
    file->position = offset;
    return 0;
}

uint32_t game_file_size(const struct game_file *file)
{
    return file->size;
}

uint32_t game_file_position(const struct game_file *file)
{
    return file->position;
}

void game_file_close(struct game_file *file)
{
    if (file && file->handle) {
        osos_file_close(file->handle);
        file->handle = NULL;
    }
}

void game_files_close_all(void)
{
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
        game_file_close(&files[i]);
}
