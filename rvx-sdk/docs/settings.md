# Settings

Settings are declared in the descriptor. The core stores them under the extension's id and keeps them across boots.

| Call | |
|---|---|
| `api->setting_get_int(self, key)` | the value, with the declared fallback as the Settings rows apply it |
| `api->setting_set_int(self, key, value)` | clamps to the declared range, stores, redraws the rows that show the setting and calls `setting_changed(key)` |
| `api->setting_read(self, key, out, capacity)` | copies at most `capacity` bytes and returns how many, 0 when absent |
| `api->setting_write(self, key, type, data, size)` | stores raw bytes, for strings and blobs; at most 255 |

## Labels

`RV_SETTING_LABELLED(key, label, fallback, labels)` names an int setting's values without a screen of its own: an `RV_ROW_SETTING` row shows the chosen label and steps to the next one when chosen, after the last back to the first, as Settings > Shuffle does. A menu entry with `.caption_setting = key` shows the label as its caption (On or Off for a bool or an unlabelled int). `RV_SETTING_CHOICE`, `RV_SETTING_SLIDER` and `RV_SETTING_MULTI` add the screen of that kind, which the row opens; `RV_SETTING_LABELLED_SCREEN` takes the screen kind and the maximum itself.

## `setting_changed`

The descriptor's `setting_changed(key)` runs after the user changes a setting and after the extension's own `setting_set_int`. It runs with `RV_SETTING_KEY_ALL` once the main module has loaded and after Reset All Settings. Build anything derived from settings, such as DSP coefficients, there.

A setting read in the main module's entry gets no fallback, because the declarations are not known yet.

## Visibility

An entry's `.shown_by` names a bool setting; the row shows while it is on. On Main and Music, Settings > Main Menu and Music Menu flip it.
