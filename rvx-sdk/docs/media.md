# Media items

`rv_media.h` reads a context menu action's `ctx->item`, the player's current item and the rows of a song model. The helpers are inline and run on the UI task.

A MediaItem is a non-owning view of a library record. One filled by `rv_player_current` or `rv_model_item` lives in `MediaItem_bytes` of the extension's own storage and is destroyed with `MediaItem_Destroy`.

| Call | |
|---|---|
| `rv_item_valid(item)` | the item views a record that is still in the library |
| `rv_item_same(a, b)` | the two view the same record; a record slot reused after a library reload does not match |
| `rv_item_value(item, slot)` | a getter of the item's vtable that returns a word, such as `MediaItem_duration_method` |
| `rv_item_text(item, slot, string)` | a getter that fills a NativeString, such as `MediaItem_title_method`: its UTF-8, NULL when empty; the caller destroys `string` once done with the text |
| `rv_player_lock()`, `rv_player_unlock(player)` | `lock` takes the MediaPlayer's mutex, which stops the playback task changing the track or list during a read, and returns the player; `unlock` releases it |
| `rv_player_current(player, item)` | the playing item, invalid when nothing plays |
| `rv_model_item(model, row, item)` | a row of a song model |
| `rv_player_position_of(player, item, &ms)` | with the player locked: 1 when `item` views the record that plays, with `ms` its position |
| `rv_controller_push(ctx->controller, screen)` | pushes an Apple screen controller above the one a callback runs in |
| `RV_METHOD(object, offset, type)` (`rv_api.h`) | the method at a vtable offset of the symbols header as a function pointer of `type` |

`rv_copy_utf8` from `rv_libc.h` copies a title into a buffer without cutting a UTF-8 sequence.
