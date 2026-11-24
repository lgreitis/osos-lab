/* SPDX-License-Identifier: GPL-3.0-only */
#include "osos.h"
#include "patch.h"

/* Only the disk-mode return caller is patched. Startup calls the same reload
 * routine directly and must never consume a reboot request. */
PATCH_ARM int cfw_disk_mode_reload(void *database)
{
    int result = ((int (*)(void *))OSOS_MEDIA_DATABASE_RELOAD)(database);
    struct osos_path path;
    osos_path_init(&path, "iPod_Control\\Device\\RepriseReboot1.req");
    void *file;
    if (osos_file_open(&path, 1, &file))
        return result;

    uint32_t size, value, read;
    int requested = !osos_file_size(file, &size) && size == sizeof(value) &&
                    !osos_file_read(file, &value, sizeof(value), &read) &&
                    read == sizeof(value) && value == 0x31524252; /* RBR1 */
    osos_file_close(file);
    if (requested && !osos_file_delete(&path) &&
        !((int (*)(unsigned))OSOS_VOLUME_SYNC)(0)) {
        ((void (*)(void))OSOS_PREPARE_RESET)();
        ((void (*)(void))OSOS_RESET_SOC_AND_WAIT)();
    }
    return result;
}
