/* SPDX-License-Identifier: MIT */

/* OpenGL ES 1.1 on the host: a software renderer ("OpenGL ES-CM 1.1",
 * renderer "Software") the app calls through the Khronos gl.h. The host owns
 * the EGL display, the context and the window surface: 320 x 240 RGB565 with
 * a 16-bit depth buffer, double buffered. It makes the context current after
 * init and before the first frame, so GL setup goes into the start callback;
 * gl* calls in init do nothing and set no error. The framebuffer a frame
 * receives is the surface's colour buffer, so GL and eapp_draw.h write the
 * same pixels, and the runtime swaps the buffers when frame returns: an app
 * creates and presents nothing itself. The EGL entries below are the host's,
 * for queries such as eglQuerySurface and eglGetError; this header is the EGL
 * declaration, the Khronos egl.h is not shipped and does not combine with it. */

#ifndef EAPP_GL_H
#define EAPP_GL_H

#include <GLES/gl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t EGLint;
typedef uint32_t EGLBoolean;
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLContext;
struct EGLNativeWindow;

#define EGL_FALSE 0
#define EGL_TRUE 1
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_SURFACE ((EGLSurface)0)

#define EGL_SUCCESS 0x3000
#define EGL_NOT_INITIALIZED 0x3001
#define EGL_BAD_ACCESS 0x3002
#define EGL_BAD_ALLOC 0x3003
#define EGL_BAD_ATTRIBUTE 0x3004
#define EGL_BAD_CONFIG 0x3005
#define EGL_BAD_CONTEXT 0x3006
#define EGL_BAD_CURRENT_SURFACE 0x3007
#define EGL_BAD_DISPLAY 0x3008
#define EGL_BAD_MATCH 0x3009
#define EGL_BAD_NATIVE_PIXMAP 0x300A
#define EGL_BAD_NATIVE_WINDOW 0x300B
#define EGL_BAD_PARAMETER 0x300C
#define EGL_BAD_SURFACE 0x300D
#define EGL_CONTEXT_LOST 0x300E

#define EGL_BUFFER_SIZE 0x3020
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_CONFIG_ID 0x3028
#define EGL_NONE 0x3038

#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054
#define EGL_EXTENSIONS 0x3055
#define EGL_HEIGHT 0x3056
#define EGL_WIDTH 0x3057
#define EGL_DRAW 0x3059
#define EGL_READ 0x305A
#define EGL_CORE_NATIVE_ENGINE 0x305B

/* GL_OES_query_matrix, which gl.h leaves to glext.h. */
GLbitfield glQueryMatrixxOES(GLfixed *mantissa, GLint *exponent);

/* The host's colour buffer has its first row at the top, where GL puts the
 * bottom, so a plain projection shows upside down against eapp_draw.h.
 * These projections swap top and bottom, which mirrors the winding, so they
 * also set the front face to clockwise: a counter-clockwise model stays a
 * front face. Both leave GL_MODELVIEW as the current matrix. perspective
 * takes the vertical field of view in degrees, above 0 and below 180, for the
 * 4:3 screen;
 * ortho_screen maps GL coordinates to screen pixels, x to the right and y
 * down from the top left, like eapp_draw.h. */
void eapp_gl_perspective(GLfloat fovy_degrees, GLfloat near, GLfloat far);
void eapp_gl_ortho_screen(void);

struct eapp_bitmap;

/* A GL_TEXTURE_2D with nearest filtering and clamped edges, bound when the
 * call returns: from a bitmap whose stride is its width, as RGB565, or from a
 * picture of the package (IMAGES as RGB without its colour key, IMAGES_ALPHA
 * as RGBA4444 for transparency), with its size into `width` and `height`
 * when given. The host takes only sides that are powers of two, as GL ES 1.1
 * allows. 0, or -1 for a missing or unfit file or a texture GL refused. */
int eapp_gl_texture_from_bitmap(const struct eapp_bitmap *bitmap, GLuint *name);
int eapp_gl_texture_from_file(const char *path, GLuint *name, uint32_t *width,
                              uint32_t *height);
/* The same from entry `index` of a loaded pack, any picture format. */
struct eapp_pack;
int eapp_gl_texture_from_pack(const struct eapp_pack *pack, unsigned index, GLuint *name,
                              uint32_t *width, uint32_t *height);

/* Khronos signatures; the host returns the same words as unsigned. */
EGLint eglGetError(void);
/* The host's display is a token: eglGetDisplay returns its argument and
 * eglGetCurrentDisplay 0. */
EGLDisplay eglGetDisplay(void *display_id);
EGLBoolean eglInitialize(EGLDisplay display, EGLint *major, EGLint *minor);
EGLBoolean eglTerminate(EGLDisplay display);
const char *eglQueryString(EGLDisplay display, EGLint name);
EGLBoolean eglGetConfigs(EGLDisplay display, EGLConfig *configs, EGLint config_size,
                         EGLint *num_config);
EGLBoolean eglChooseConfig(EGLDisplay display, const EGLint *attrib_list, EGLConfig *configs,
                           EGLint config_size, EGLint *num_config);
EGLBoolean eglGetConfigAttrib(EGLDisplay display, EGLConfig config, EGLint attribute,
                              EGLint *value);
EGLSurface eglCreateWindowSurface(EGLDisplay display, EGLConfig config,
                                  const struct EGLNativeWindow *window,
                                  const EGLint *attrib_list);
EGLSurface eglCreatePixmapSurface(EGLDisplay display, EGLConfig config,
                                  const struct EGLNativeWindow *pixmap,
                                  const EGLint *attrib_list);
EGLSurface eglCreatePbufferSurface(EGLDisplay display, EGLConfig config,
                                   const EGLint *attrib_list);
EGLBoolean eglDestroySurface(EGLDisplay display, EGLSurface surface);
EGLBoolean eglQuerySurface(EGLDisplay display, EGLSurface surface, EGLint attribute,
                           EGLint *value);
/* The host answers EGL_FALSE to these four. */
EGLBoolean eglSurfaceAttrib(EGLDisplay display, EGLSurface surface, EGLint attribute,
                            EGLint value);
EGLBoolean eglBindTexImage(EGLDisplay display, EGLSurface surface, EGLint buffer);
EGLBoolean eglReleaseTexImage(EGLDisplay display, EGLSurface surface, EGLint buffer);
EGLBoolean eglSwapInterval(EGLDisplay display, EGLint interval);
EGLContext eglCreateContext(EGLDisplay display, EGLConfig config, EGLContext share_context,
                            const EGLint *attrib_list);
EGLBoolean eglDestroyContext(EGLDisplay display, EGLContext context);
EGLBoolean eglMakeCurrent(EGLDisplay display, EGLSurface draw, EGLSurface read,
                          EGLContext context);
EGLContext eglGetCurrentContext(void);
EGLSurface eglGetCurrentSurface(EGLint readdraw);
EGLDisplay eglGetCurrentDisplay(void);
EGLBoolean eglQueryContext(EGLDisplay display, EGLContext context, EGLint attribute,
                           EGLint *value);
EGLBoolean eglWaitGL(void);
EGLBoolean eglWaitNative(EGLint engine);
/* The runtime calls this after every frame; an app does not. */
EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface);
EGLBoolean eglCopyBuffers(EGLDisplay display, EGLSurface surface, void *target);

#ifdef __cplusplus
}
#endif

#endif
