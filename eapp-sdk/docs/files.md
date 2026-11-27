# Files

Twelve files and four folders can be open at once, newlib descriptors included. `eapp_file_error` answers the host's status of the last failed file, data, folder or save call, as the `FileStatus_*` and `GameSaveStatus_*` values of the symbols header.

## Package files

`eapp_file_open`, `eapp_file_read`, `eapp_file_seek`, `eapp_file_size`, `eapp_file_position` and `eapp_file_close` read a file of the package. Paths are relative to the package, as `FILES` lists them. `eapp_file_read_all(path, buffer, capacity)` reads a whole file without a handle.

## Data files

The app's data folder is `/Reprise/Data/Games/gamedata_RW/<id>/` or `/Reprise/Data/Apps/appdata_RW/<id>/` under RepriseOS, and `iPod_Control/gamedata_RW/<id>/` elsewhere.

| Call | |
|---|---|
| `eapp_data_open(name, mode)` | Opens a file there for reading or writing. Writing creates the folder and replaces the file |
| `eapp_file_write(file, data, size)` | Writes to it. The bytes are on disk once the handle is closed |
| `eapp_data_delete(name)` | Deletes it |
| `eapp_data_read`, `eapp_data_write` | Read or replace a whole file without a handle |

A whole-file read copies what fits and returns the file's full size.

## Save store

`eapp_save_open`, `eapp_save_read`, `eapp_save_write`, `eapp_save_size` and `eapp_save_close` use Apple's save store: a file in the data folder that the host holds in memory. The device has four slots with 100 KiB together. `write` replaces the contents, which the host writes to disk at the next system save or app launch, not at exit. Use a data file for anything larger or that must be on disk at exit.

`eapp_save_record_write` and `eapp_save_record_read` keep one struct in the save store behind a header with a magic, a version and a checksum. A changed layout or a damaged file reads as no record.

## Folders

`eapp_directory_open(path, recursive)`, `eapp_directory_read` and `eapp_directory_close` list a folder. Names are relative to the folder opened, subfolders come depth first. A recursive listing goes 7 levels below the folder opened and 500 entries per folder; deeper and later entries are skipped.
