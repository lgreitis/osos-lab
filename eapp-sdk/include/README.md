# Khronos headers

The OpenGL ES 1.1 headers an app includes as `<GLES/gl.h>`, unchanged from the Khronos registries:

| File | From | Licence |
|---|---|---|
| `GLES/gl.h` | [OpenGL-Registry](https://github.com/KhronosGroup/OpenGL-Registry) `api/GLES/gl.h`, commit 6af574a | MIT, `LICENSE-MIT.txt` |
| `GLES/glplatform.h` | OpenGL-Registry `api/GLES/glplatform.h`, commit 6af574a | Apache-2.0, `LICENSE-Apache-2.0.txt` |
| `KHR/khrplatform.h` | [EGL-Registry](https://github.com/KhronosGroup/EGL-Registry) `api/KHR/khrplatform.h`, commit 8e56e86 | MIT, `LICENSE-MIT.txt` |

`eapp_gl.h` in the directory above includes `gl.h` and declares the host's EGL entries. The [guide](../docs/opengl.md) lists the `gl.h` functions the host lacks.
