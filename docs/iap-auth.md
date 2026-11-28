# iAP accessory authentication bypass

Addresses are for Apple 2.0.5 OSOS (the image the installer caches as `osos-2.0.5.bin`,
matching the `inputs/osos.bin` fingerprint in [`firmware/apple.json`](../firmware/apple.json)).
Function names prefixed `FUN_` are Ghidra auto-analysis labels, not official symbols.

## Goal

Let custom accessories use authentication-gated iAP (iPod Accessory Protocol) lingos and
commands without a Made-for-iPod authentication coprocessor ("CP chip"). The native firmware
challenges an accessory to sign a nonce with its CP chip and verifies that signature before
promoting the accessory to the authenticated state; a home-made accessory has no chip and so
is refused the gated lingos. The iAP Auth Bypass extension makes that verification always
succeed.

## How iAP authentication works

iAP frames arrive over the dock serial/USB transport and are dispatched by lingo and command
ID. Authentication is a challenge/response exchange in the General Lingo (`0x00`):

1. On attach, the iPod sends `GetAccessoryInfo` (`0x38`) and the device-authentication
   requests `GetDevAuthenticationInfo` (`0x14`) and `GetDevAuthenticationSignature` (`0x17`).
2. The accessory replies with `RetDevAuthenticationInfo` (`0x15`, its certificate) and then
   `RetDevAuthenticationSignature` (`0x18`, the nonce signed by its CP chip).
3. The iPod verifies the signature against the certificate. On success it marks the accessory
   authenticated and unlocks the gated lingos; on failure it refuses them.

### Per-accessory state

Each connected accessory has a 0x24-byte record in a table (base resolved from
`DAT_08188a48` → `0x08a2dc3c`, stride `0x24`; up to three accessories). Record byte `+4` holds
the authentication state. The state machine that drives it is `FUN_08185e18(ctx, acc, state)`;
state `6` is "authenticated". The packet accessors are `FUN_080ebd1c(pkt)` (accessory id),
`FUN_080ec040` (length) and `FUN_080ebd34` (payload pointer); a parsed iAP packet carries its
payload pointer at `+8` and length at `+0xc`.

### The verification gate

`FUN_081892b8` handles `RetDevAuthenticationSignature` (`0x18`). Simplified:

```c
rec    = record_base + acc_id * 0x24;   /* rec[+4] = auth state byte */
status = 4;                             /* non-zero = reject */
...
if (rec[+0x1c] == 1)      result = FUN_0812dca8(... signature ...);  /* RSA-style verifier */
else if (rec[+0x1c] == 2) result = FUN_081592fc(... signature ...);  /* alternate verifier  */

if (result == 0)          status = 0;   /* verification passed */
send_ack(status);                       /* General Lingo 0x19 AckDevAuthenticationStatus */
if (status == 0)
    FUN_08185e18(ctx, acc_id, 6);       /* promote accessory to authenticated */
```

Both verifiers return `0` on success and non-zero on failure, and both are reached **only** from
`FUN_081892b8`:

| Verifier | Address | Mechanism |
| --- | --- | --- |
| `FUN_0812dca8` | `0x0812dca8` | RSA-style: `FUN_0812e228` (context), `FUN_082c46c8` (hash), `FUN_082c4740` (verify) |
| `FUN_081592fc` | `0x081592fc` | Alternate: `FUN_082cb2e0` |

The verifier result is the sole gate: a `0` authenticates the accessory, anything else refuses it.

## The extension

The core does not patch the verifiers. The iAP Auth Bypass extension binds the two call sites
in `FUN_081892b8` that reach them, listed in `payload/rvx/site_table.inc`:

| Site | Address | Original instruction |
| --- | --- | --- |
| `IAP_VerifyDeviceSignatureRSA_call` | `0x081893a8` | `bl FUN_0812dca8` |
| `IAP_VerifyDeviceSignatureAlternate_call` | `0x081893fc` | `bl FUN_081592fc` |

The extension's replacement returns `0` from either call, so the handler treats every
signature as verified. The verifier functions themselves stay unmodified, and as both are
reached only from these two calls, nothing else observes the change.

## Behaviour and limits

A home-made accessory must still **walk the handshake** — reply to `GetDevAuthenticationInfo`
with `RetDevAuthenticationInfo` (`0x15`) and to `GetDevAuthenticationSignature` with
`RetDevAuthenticationSignature` (`0x18`) — but the contents no longer need to be chip-signed;
any bytes pass, and the accessory reaches the authenticated state.
