/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from RevolaOS, copyright 2026 The RevolaOS Authors.
 */

/* Media-list nodes count the tracks and containers below them in two halfwords,
 * +0x2c (containers) and +0x2e (tracks), which caps a library at 65,535 tracks: the
 * import wraps the count, the sort indexes fail to build and the import aborts. Every
 * access moves to a 32-bit track count at +0x2c and the container count to the unused
 * flags halfword at +0x1e (+0x14 is taken: containers keep their lookup key there).
 * Constant words, so the recipe writes them. The symbols file keeps Apple's
 * MediaEntry layout; after these words extensions read tracks as a word at
 * MediaEntry_groups and the container count as a halfword at 0x1e. */

#include "../rvx-sdk/osos-2.0.5.h"
#include "patch.h"

/* PATCH_RECORD rather than PATCH_WORD: a reservation would cost a word of payload
 * each, and a raw extension site at one of these addresses fails anyway because
 * the word it expects is no longer there. */
#define TRACK_COUNT_WORD(site, expected, value)                                        \
    PATCH_RECORD(1, site, expected, 0, value, "")

/* r8,[r0,#0x2e] -> ldrne r8,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0803b2d8_word, 0x11d082be, 0x1590802c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0803bf9c_word, 0x11d002be, 0x1590002c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0803c9bc_word, 0x11d002be, 0x1590002c);
/* r6,[r0,#0x2e] -> ldrne r6,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0804789c_word, 0x11d062be, 0x1590602c);
/* r0,[r0,#0x2c] -> ldrhne r0,[r0,#0x1e] */
TRACK_COUNT_WORD(SITE_TrackCount_0804793c_word, 0x11d002bc, 0x11d001be);
/* r1,[r1,#0x2e] -> ldrne r1,[r1,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_08047970_word, 0x11d112be, 0x1591102c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_08047c7c_word, 0x11d002be, 0x1590002c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_08048e08_word, 0x11d002be, 0x1590002c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_08048e48_word, 0x11d002be, 0x1590002c);
/* r0,[r0,#0x2e] -> ldr r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_08049070_word, 0xe1d002be, 0xe590002c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_08049368_word, 0x11d002be, 0x1590002c);
/* r7,[r0,#0x2e] -> ldrne r7,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_08049938_word, 0x11d072be, 0x1590702c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080499c4_word, 0x11d002be, 0x1590002c);
/* r0,[r0,#0x2e] -> ldr r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0804a180_word, 0xe1d002be, 0xe590002c);
/* r7,[r0,#0x2e] -> ldrne r7,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_08051eb8_word, 0x11d072be, 0x1590702c);
/* r7,[r0,#0x2e] -> ldrne r7,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080562f0_word, 0x11d072be, 0x1590702c);
/* r1,[r1,#0x2e] -> ldrne r1,[r1,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080586c0_word, 0x11d112be, 0x1591102c);
/* r1,[r1,#0x2e] -> ldrne r1,[r1,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080587b0_word, 0x11d112be, 0x1591102c);
/* r1,[r1,#0x2e] -> ldrne r1,[r1,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_08059788_word, 0x11d112be, 0x1591102c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0805cb04_word, 0x11d002be, 0x1590002c);
/* r1,[r1,#0x2e] -> ldrne r1,[r1,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0805cf70_word, 0x11d112be, 0x1591102c);
/* r6,[r1,#0x2e] -> ldr r6,[r1,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0806294c_word, 0xe1d162be, 0xe591602c);
/* r4,[r0,#0x2e] -> ldrne r4,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080637d4_word, 0x11d042be, 0x1590402c);
/* r8,[r0,#0x2e] -> ldrne r8,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_08063834_word, 0x11d082be, 0x1590802c);
/* r0,[r0,#0x2e] -> ldr r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0807b8b8_word, 0xe1d002be, 0xe590002c);
/* r0,[r0,#0x2e] -> ldr r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0807b8f0_word, 0xe1d002be, 0xe590002c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0807c87c_word, 0x11d002be, 0x1590002c);
/* r0,[r0,#0x2e] -> ldr r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_08082248_word, 0xe1d002be, 0xe590002c);
/* r0,[r0,#0x2e] -> ldr r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_08082280_word, 0xe1d002be, 0xe590002c);
/* r0,[r0,#0x2e] -> ldr r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080822a8_word, 0xe1d002be, 0xe590002c);
/* r0,[r0,#0x2e] -> ldr r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0808232c_word, 0xe1d002be, 0xe590002c);
/* r0,[r5,#0x2c] -> ldrhne r0,[r5,#0x1e] */
TRACK_COUNT_WORD(SITE_TrackCount_08089058_word, 0x11d502bc, 0x11d501be);
/* r7,[r5,#0x2e] -> ldrne r7,[r5,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0808905c_word, 0x11d572be, 0x1595702c);
/* r0,[r4,#0x2e] -> ldr r0,[r4,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0808909c_word, 0xe1d402be, 0xe594002c);
/* r0,[r4,#0x2e] -> str r0,[r4,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080890a4_word, 0xe1c402be, 0xe584002c);
/* r0,[r4,#0x2c] -> ldrh r0,[r4,#0x1e] */
TRACK_COUNT_WORD(SITE_TrackCount_080890a8_word, 0xe1d402bc, 0xe1d401be);
/* r0,[r4,#0x2c] -> strh r0,[r4,#0x1e] */
TRACK_COUNT_WORD(SITE_TrackCount_080890b0_word, 0xe1c402bc, 0xe1c401be);
/* r1,[r0,#0x2e] -> ldr r1,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_08092e24_word, 0xe1d012be, 0xe590102c);
/* r0,[r0,#0x2c] -> ldrh r0,[r0,#0x1e] */
TRACK_COUNT_WORD(SITE_TrackCount_08092e28_word, 0xe1d002bc, 0xe1d001be);
/* r1,[r0,#0x2e] -> ldr r1,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080931b0_word, 0xe1d012be, 0xe590102c);
/* r0,[r0,#0x2c] -> ldrh r0,[r0,#0x1e] */
TRACK_COUNT_WORD(SITE_TrackCount_080931b4_word, 0xe1d002bc, 0xe1d001be);
/* r0,[r5,#0x2c] -> ldrhne r0,[r5,#0x1e] */
TRACK_COUNT_WORD(SITE_TrackCount_0809cdc0_word, 0x11d502bc, 0x11d501be);
/* r7,[r5,#0x2e] -> ldrne r7,[r5,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0809cdc4_word, 0x11d572be, 0x1595702c);
/* r0,[r4,#0x2e] -> ldr r0,[r4,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0809cdd4_word, 0xe1d402be, 0xe594002c);
/* r0,[r4,#0x2e] -> str r0,[r4,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0809cddc_word, 0xe1c402be, 0xe584002c);
/* r0,[r4,#0x2c] -> ldrh r0,[r4,#0x1e] */
TRACK_COUNT_WORD(SITE_TrackCount_0809cde0_word, 0xe1d402bc, 0xe1d401be);
/* r0,[r4,#0x2c] -> strh r0,[r4,#0x1e] */
TRACK_COUNT_WORD(SITE_TrackCount_0809cde8_word, 0xe1c402bc, 0xe1c401be);
/* r9,[r0,#0x2e] -> ldrne r9,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0809f69c_word, 0x11d092be, 0x1590902c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080b3460_word, 0x11d002be, 0x1590002c);
/* r1,[r1,#0x2e] -> ldrne r1,[r1,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080b3dcc_word, 0x11d112be, 0x1591102c);
/* r1,[r1,#0x2e] -> ldrne r1,[r1,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080b3dec_word, 0x11d112be, 0x1591102c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080bf914_word, 0x11d002be, 0x1590002c);
/* r2,[r2,#0x2e] -> ldrne r2,[r2,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080bf9b8_word, 0x11d222be, 0x1592202c);
/* r0,[r1,#0x2e] -> ldr r0,[r1,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080d16b4_word, 0xe1d102be, 0xe591002c);
/* r1,[r1,#0x2c] -> ldrh r1,[r1,#0x1e] */
TRACK_COUNT_WORD(SITE_TrackCount_080d16b8_word, 0xe1d112bc, 0xe1d111be);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_080d43cc_word, 0x11d002be, 0x1590002c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_081087e8_word, 0x11d002be, 0x1590002c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0810897c_word, 0x11d002be, 0x1590002c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0816ebc4_word, 0x11d002be, 0x1590002c);
/* r0,[r0,#0x2e] -> ldrne r0,[r0,#0x2c] */
TRACK_COUNT_WORD(SITE_TrackCount_0829c884_word, 0x11d002be, 0x1590002c);
