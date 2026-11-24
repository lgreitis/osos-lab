# Raw sites

For code no core site covers, the manifest can declare its own sites. They are exclusive, checked against the firmware at load, and share a pool of eight. Ask for a core site when others could use it too. First-party extensions use core sites only.

```toml
[[site]]
name = "coverflow_clear"     # a label for the site
address = 0x08123456         # the word to patch, from the pinned firmware
kind = "call"                # call, vtable or word
expected = 0xEB001234        # the word there now; the core refuses a mismatch
argc = 2                     # register arguments, at most 4; 0 for word
```

Bind the n-th declared site as `RV_RAW_SITE(n)` in a hook declaration. Stack arguments are not supported.

| Refusal in Settings > Extensions | Cause |
|---|---|
| Site mismatch | the address is outside the OSOS image below the heap, or does not hold the expected word |
| Conflict | the address lies inside a core site, which is named, or another extension's raw site |
| No memory | the pool of eight is used up |

A released site can be reused two seconds later. See [Hook sites](https://github.com/paviro/revola/blob/main/docs/core/hook-sites.md) for the core side.
