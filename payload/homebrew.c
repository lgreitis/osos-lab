/* SPDX-License-Identifier: GPL-3.0-only */

#include "osos.h"
#include "patch.h"

PATCH_CALL(0x080F9360, 0x0825CCAC, cfw_game_manifest_reader);

static int is_homebrew_manifest(const char *path)
{
    static const char root[] = "iPod_Control/games_RO/";
    static const char filename[] = "/Manifest.plist";

    if (!path)
        return 0;

    for (unsigned int i = 0; root[i]; i++)
        if (*path++ != root[i])
            return 0;

    if (!*path || *path == '/' || *path == '.')
        return 0;

    while (*path && *path != '/') {
        if (*path == '\\' || *path == ':')
            return 0;
        path++;
    }

    unsigned int i = 0;
    while (filename[i] && path[i] == filename[i])
        i++;

    return filename[i] == 0 && path[i] == 0;
}

PATCH_ARM void cfw_game_manifest_reader(void *reader, const char *manifest,
                                        uint8_t mode, const char *signature)
{
    if (mode == 0 && is_homebrew_manifest(manifest))
        signature = NULL;

    osos_game_manifest_reader_init(reader, manifest, mode, signature);
}
