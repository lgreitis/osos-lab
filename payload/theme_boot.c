/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_boot.h"
#include "theme_classic.h"
#include "theme_discovery.h"
#include "theme_preferences.h"
#include "preferences.h"

const struct osos_path cfw_theme_classic_baseline = {0, "themes/classic/default.bin"};
const struct osos_path cfw_theme_classic_dark = {0, "themes/classic/dark.bin"};

int cfw_theme_boot_resolve(struct cfw_theme_boot_request *request)
{
    struct cfw_theme_preferences saved;
    enum cfw_theme_preferences_status status = cfw_theme_preferences_load(&saved);
    uint32_t legacy = 0;
    if (status == CFW_THEME_PREF_ABSENT) {
        if (!cfw_preferences_read("iPod_Control\\Device\\CFWDark", &legacy, sizeof(legacy)))
            legacy = 0;
    }
    unsigned char *bytes = (unsigned char *)request;
    for (unsigned i = 0; i < sizeof(*request); i++)
        bytes[i] = 0;
    request->appearance_mask = 3;
    request->migrate = status == CFW_THEME_PREF_ABSENT;
    if (cfw_theme_boot_selection(status, &saved, legacy, &request->selection))
        return 1;
    if (!request->selection.id[0]) {
        if (request->selection.dark) {
            request->pack.volume = cfw_theme_classic_dark.volume;
            for (unsigned i = 0; i < sizeof(request->pack.text); i++)
                request->pack.text[i] = cfw_theme_classic_dark.text[i];
        }
        return 0;
    }
    struct cfw_theme_metadata metadata;
    uint32_t dark;
    if (cfw_theme_discovery_select(request->selection.id, request->selection.dark, 0,
                                   &metadata, &request->pack, &dark)) {
        request->appearance_mask = metadata.appearance_mask;
        for (unsigned i = 0; i < sizeof(request->name); i++)
            request->name[i] = metadata.name[i];
        return 0;
    }
    /* Missing or changed packages recover exactly as malformed preferences do. */
    cfw_theme_boot_selection(CFW_THEME_PREF_INVALID, 0, 0, &request->selection);
    return 1;
}
