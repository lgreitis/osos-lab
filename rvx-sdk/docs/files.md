# Files

Each extension has a data folder, `/Reprise/Data/Extensions/<id>/`. Games and apps keep theirs in `/Reprise/Data/Games/gamedata_RW/<id>/` and `/Reprise/Data/Apps/appdata_RW/<id>/`, where Homebrew points the host's game-data folder. Paths cannot leave the folder. The core closes an extension's open files when it unloads.

For files the listener copies onto the iPod, an extension names a content folder in its manifest, `content = "Themes"` (or a list). It then reads `/Reprise/Themes/` with `api->content_open` and `api->content_list`, using names such as `Themes/example.rvt`. Content folders are read-only and have one owner. A second enabled extension naming the same folder does not load and shows Conflict in Settings > Extensions.

`api->firmware_open` opens the booted firmware, `/Reprise/Firmware/reprise.bin`, where themes read stock artwork. Handles from `content_open`, `firmware_open` and `asset_open` ([Assets](assets.md)) work with `file_read`, `file_seek` and `file_close`.

For anything else on the disk, use Apple's file functions from the symbols header.

| Call | |
|---|---|
| `api->file_open(self, name, mode, &file)` | opens a path inside the folder for reading or writing; writing creates the folders above it |
| `api->file_read`, `api->file_write`, `api->file_seek`, `api->file_close` | work on the handle |
| `api->file_delete` | removes a file |
| `api->folder_list` | lists a folder |
| `api->content_open(self, name, &file)` | opens a file of a content folder for reading |
| `api->content_list(self, name, each, arg)` | lists a content folder, or a folder inside it |
| `api->firmware_open(self, &file)` | opens the booted firmware image for reading |
