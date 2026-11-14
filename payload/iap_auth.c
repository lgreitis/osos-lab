/* SPDX-License-Identifier: GPL-3.0-only */

#include "patch.h"
#include "patches/sites.h"

/* iAP accessory authentication bypass.
 *
 * The native RetDevAuthenticationSignature (General Lingo 0x18) handler verifies
 * the accessory's challenge signature through one of two wrappers, selected by the
 * accessory's declared auth version: the RSA-style verifier at
 * OSOS_IAP_AUTH_VERIFY_A and the alternate verifier at OSOS_IAP_AUTH_VERIFY_B.
 * Both return 0 on success and are called only from that handler, which promotes
 * the accessory to the authenticated state when the result is 0.
 *
 * Returning 0 unconditionally makes every accessory authenticate, so a Made-for-iPod
 * coprocessor is not required to use authentication-gated lingos and commands. */
PATCH_ARM int cfw_iap_auth_verify(void)
{
    return 0;
}
