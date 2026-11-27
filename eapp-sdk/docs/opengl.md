# OpenGL ES

The host renders OpenGL ES 1.1 in software. `glGetString` answers `OpenGL ES-CM 1.1`, renderer `Software`, with the `GL_OES_fixed_point`, `single_precision`, `read_format`, `query_matrix`, `point_size_array`, `point_sprite` and `compressed_paletted_texture` extensions. `#include "eapp_gl.h"` brings in the Khronos `<GLES/gl.h>` and declares the host's EGL entries, which are for queries only. Floats work with the SDK's soft-float ABI.

## Context and frames

After `init` the host creates the context and a double-buffered 320 x 240 RGB565 window surface with a 16-bit depth buffer. It tears them down after the last frame. GL calls in `init` do nothing, so one-time setup goes into `start`.

The frame's buffer is the surface's colour buffer, so GL and [drawing](drawing.md) calls land in the same pixels. The runtime swaps buffers when `frame` returns.

- Call `glFinish` before drawing calls go over GL output or the app copies it, or they land under it.
- The surface has two buffers, so a frame that draws nothing shows the frame before last. An app that skips frames keeps both buffers filled or redraws every frame.
- The colour buffer's first row is the top of the screen, where GL puts the bottom, so a plain projection shows upside down against the drawing calls.

`eapp_gl_perspective(fovy_degrees, near, far)` and `eapp_gl_ortho_screen()` set a projection with top and bottom swapped and the front face clockwise, so a counter-clockwise model stays a front face. The field of view is above 0 and below 180 degrees. `ortho_screen` maps GL coordinates to screen pixels, y down from the top left, like the drawing calls. Both leave `GL_MODELVIEW` current.

## Textures

| Call | Texture from |
|---|---|
| `eapp_gl_texture_from_bitmap` | A `struct eapp_bitmap` whose stride is its width |
| `eapp_gl_texture_from_pack` | An entry of a loaded pack |
| `eapp_gl_texture_from_file` | A picture of the package: `IMAGES` as RGB565 without its colour key, `IMAGES_ALPHA` as RGBA4444 for transparency |

The host refuses a texture whose sides are not powers of two. GL keeps its own copy of every texture in the app's heap.

## Host faults

- Turning the colour mask off and on again leaves the next draw black. Write depth only through `glBlendFunc(GL_ZERO, GL_ONE)` instead.
- Back faces are lit before culling, so a lit model with culling on shows lighting artefacts unless the app culls on its own.

## Missing entries

The runtime supplies `glColor4ub`, `glTexEnvi`, `glTexEnviv`, `glTexParameterfv`, `glTexParameteriv` and `glTexParameterxv`, which the host lacks. `glPointSizePointerOES` is missing. `glCompressedTexSubImage2D`, `glGetTexParameterfv` and `glGetTexParameterxv` only raise a GL error.
