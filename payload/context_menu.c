/* SPDX-License-Identifier: GPL-3.0-only */

#include "cfw.h"
#include "patch.h"

/* Zero selects native screen navigation for the expected action, Song Info or chapters,
 * suppressing the dismissal frame.
 * An action that cannot open its screen must restore the source layout. */
PATCH_ARM int cfw_context_menu_screen_action(const char *action, const char *expected)
{
    return !(cfw_string_equal(action, expected) ||
             cfw_string_equal(action, "CFW_SongInfo") ||
             cfw_string_equal(action, "CFW_NowPlayingChapters") ||
             cfw_string_equal(action, "CFW_EpisodeChapters"));
}
