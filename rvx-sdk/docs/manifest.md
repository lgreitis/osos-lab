# Manifest

`manifest.toml` names the extension, its modules and its assets. `rv.mk` reads the id, the modules, the required libraries and the asset files from it; `rvx-pack` checks it and compiles it into the `.rvx`. The full format, with every rule, is the docstring of `manifest.py` in the SDK.

| Key | |
|---|---|
| `id` | `[a-z][a-z0-9_-]{0,31}`, the file name before `.rvx` and the settings owner |
| `name`, `description` | what Settings > Extensions shows; the description is one plain sentence |
| `version`, `author`, `license` | shown on the extension's page; the licence is GPL-3.0 compatible |
| `api` | the oldest core API major.minor the extension runs on, see [Versions](modules.md#versions) |
| `category` | where Settings > Extensions lists it: `effects`, `media`, `system`, `other`, or `library` for shared code |
| `stability` | optional: `alpha`, `beta` or `stable`, the author's word |
| `note` | optional: one line under the version, such as "Ported from Rockbox" |
| `content` | optional: a folder under `/Revola/` the extension reads, or a list of them; one owner per folder |
| `requires` | optional: the ids of up to four library extensions whose functions the modules call |
| `exports` | a library's functions, by C name |
| `[[module]]` | `name`, `trigger` (`always` for the first, `main`; `screen` with `screens`; `manual`), and `preload` for a module after main |
| `[[asset]]` | `name`, `file`, and optionally `pane`, `preload` and `compress`, see [Assets](assets.md) |
| `[[site]]` | a raw hook site, see [Raw sites](raw-sites.md) |

```toml
id = "balance"
name = "Balance"
description = "Shift the sound towards the left or right ear in decibel steps, or play one ear only."
version = "0.1.0"
author = "RevolaOS"
license = "GPL-3.0-only"
api = "1.78"
category = "effects"

[[module]]
name = "main"
trigger = "always"

[[module]]
name = "editor"
trigger = "screen"
screens = ["editor"]

[[asset]]
name = "settings-balance"
file = "assets/settings-balance.png"
pane = "icon"
```
