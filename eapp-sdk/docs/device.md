# Device

| Call | |
|---|---|
| `eapp_alloc`, `eapp_realloc`, `eapp_free` | Blocks of the host's heap, at most 8 MiB each, all freed when the app ends |
| `eapp_ticks_ms` | Milliseconds since the app's init |
| `eapp_sleep_ms` | Blocks for at least that long. 0 yields |
| `eapp_platform` | iPod video, nano 3G or classic, the same value as `GamesPlatformID` in SysInfoExtended. The emulator reports the classic |
| `eapp_external_power` | A computer or charger on the cable. The emulator counts only its adapter mode |
| `eapp_battery_level` | 0 to 100 in the host's 22 steps, -1 before the host has a reading. The host is asked at most once a second |
| `eapp_language` | The locale code of the Settings > Language entry, below |
| `eapp_name` | The package's name as Extras lists it, in the current language when the manifest has it |
| `eapp_log`, `eapp_logf` | A line on the host's debug console, `qemu.log` in the emulator's run folder |
| `eapp_format`, `eapp_format_list` | `%d %i %u %x %X %c %s %%` with the `-` and `0` flags, a width and a precision for `%s`, into a buffer. Returns the whole length like `snprintf` |

## Clock

`eapp_date_time` fills year, month, day, hour, minute and second in local time. Until the user sets the clock, the host reports a fixed default date. `eapp_clock_24h` reads the clock setting.

`eapp_format_time` writes "9:05 PM" or "21:05" in at most `EAPP_TIME_TEXT_SIZE` bytes: no leading zero and ` AM`/` PM` in 12-hour mode, two digits in 24-hour mode, all in characters the SDK font has. There is no date formatter.

## Backlight

`eapp_set_backlight_level` (0 to 100) changes the user's Brightness setting, which stays after the app ends. The level query reports only what the app set, so an app cannot restore the user's value. Set it only when the user asks.

`eapp_set_backlight_dimmed` caps the level at 20 and ends with the app. The backlight timer of Settings keeps running; an app cannot keep the backlight on or turn it off.

## Languages

`eapp_language` answers one of en_US, da_DK, de_CH, de_DE, es_ES, fi_FI, fr_BE, fr_CH, fr_FR, it_IT, ja_JP, ko_KR, nl_BE, nl_NL, no_NO, pt_PT, sv_SE, zh_CN, zh_TW, cs_CZ, el_GR, hu_HU, pl_PL, ru_RU and tr_TR.
