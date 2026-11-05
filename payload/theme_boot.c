/* SPDX-License-Identifier: GPL-3.0-only */
#include "theme_boot.h"
#include "cfw.h"
#include "theme_classic.h"
#include "theme_discovery.h"
#include "theme_preferences.h"

int cfw_theme_boot_resolve(struct cfw_theme_boot_request *request)
{
    struct cfw_theme_preferences saved;
    enum cfw_theme_preferences_status status = cfw_theme_preferences_load(&saved);
    cfw_zero(request, sizeof(*request));
    request->appearance_mask = 3;
    if (cfw_theme_boot_selection(status, &saved, &request->selection))
        return 1;
    if (!request->selection.id[0]) {
        if (request->selection.dark)
            cfw_theme_path(&request->pack, CFW_THEME_CLASSIC_DARK);
        return 0;
    }
    struct cfw_theme_metadata metadata;
    uint32_t dark;
    if (cfw_theme_discovery_select(request->selection.id, request->selection.dark, 0,
                                   &metadata, &request->pack, &dark)) {
        request->appearance_mask = metadata.appearance_mask;
        cfw_copy(request->name, metadata.name, sizeof(request->name));
        return 0;
    }
    /* Missing or changed packages recover exactly as malformed preferences do. */
    cfw_theme_boot_selection(CFW_THEME_PREF_INVALID, 0, &request->selection);
    return 1;
}
