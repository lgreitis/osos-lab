# eApp SDK

An app is a flat image that Apple's eApp host, the runtime behind the iPod games from iTunes, loads into a 1 MiB mapping and drives with a frame loop. A package is that image, a `Manifest.plist` and a 55 x 55 icon in one folder. The Homebrew extension lists packages from `/Reprise/Games/<id>/` and `/Reprise/Apps/<id>/` under Extras and lets the host load them without an Apple signature. The image reaches the host only through its import list, so the same package runs from `iPod_Control/games_RO/<id>/` on any 2.0.5 firmware that skips the game signature check.

| Page | Contents |
|---|---|
| [Build](build.md) | Makefile variables, the package, limits, using a C library |
| [Input](input.md) | Callbacks, buttons, the wheel, menus, screens, the file picker |
| [Drawing](drawing.md) | The framebuffer, shapes, bitmaps and packs, text |
| [OpenGL ES](opengl.md) | The host's software renderer |
| [Maths](maths.md) | Fixed point, angles, easing, random numbers |
| [Files](files.md) | Package files, data files, the save store, folders |
| [Sound](sound.md) | Sound samples mixed over the music |
| [Music](music.md) | The iPod's player and the app's own playback list |
| [Library](library.md) | Songs, playlists, covers and photos, the player list |
| [Device](device.md) | Clock, backlight, language, battery, heap, log |
| [Modules](modules.md) | Code outside the 1 MiB image, loaded into the heap |
| [Image](image.md) | The image layout, the import list, picture, pack and module files |

Adapted from the [RepriseOS game SDK](https://github.com/lgreitis/osos-lab/tree/main/game-sdk) by lgreitis.

## A first app

`main.c` defines `eapp_application`, the callbacks of [Input](input.md). `frame` gets the RGB565 buffer to draw into, shown when the call returns.

```c
#include "eapp.h"
#include "eapp_ui.h"

static void input(const struct eapp_input_event *event)
{
    if (event->button == EAPP_BUTTON_MENU && event->type == EAPP_INPUT_PRESS)
        eapp_request_exit();
}

static void frame(const struct eapp_frame *frame)
{
    eapp_clear(&frame->framebuffer, 0);
    eapp_draw_text(&frame->framebuffer, 20, 20, "HELLO", 0xffff, 1);
}

const struct eapp eapp_application = {
    .input = input, .frame = frame,
};
```

`Makefile` names the package and includes `eapp.mk`:

```make
ID := hello
NAME := Hello
KIND := game          # game for /Reprise/Games/, app for /Reprise/Apps/
SOURCES := main.c
ICON := icon.png
include $(SDK)/eapp.mk
```

```sh
make SDK=/path/to/eapp-sdk
```

`build/hello/` is the package. Copy it to `/Reprise/Games/hello/` on the iPod, or onto an emulator disk with `ipodvm disk --copy`.

## Examples

| Example | Shows |
|---|---|
| `hello` | Text, a picture, a sound built by `tone.py`, a name per language, the clock, device and music player values, the backlight by wheel, dimming while Center is held, one measured `eapp_sleep_ms(500)` |
| `cube` | A turning cube with the depth test, `glGetString` and `GL_DEPTH_BITS` |
| `library` | Artists, playlists and tracks with the highlighted cover, playing from a track on, the photos, the player list |
| `module` | Two effects, each a module that calls the image's drawing and reads its frame count; Center frees one and loads the other |
