/* SPDX-License-Identifier: MIT */

/* The gl.h entries the host's table lacks, on the entries it has. GL ES 1.1
 * defines them as the same operations with converted arguments: a byte
 * colour maps to 0..1, an integer colour component to (2 i + 1) / (2^32 - 1),
 * an enum passes through, and every texture parameter is a scalar, so a
 * vector form reads its first element. glPointSizePointerOES has no host
 * entry at all. */

#include <GLES/gl.h>

/* Not platform.h: the symbols header's GL prototypes differ from gl.h's. */
#include "eapp_draw.h"
#include "eapp_gl.h"
#include "eapp_libc.h"
#include "eapp_math.h"
#include "picture.h"

void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    glColor4f(red / 255.0f, green / 255.0f, blue / 255.0f, alpha / 255.0f);
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    glTexEnvf(target, pname, (GLfloat)param);
}

void glTexEnviv(GLenum target, GLenum pname, const GLint *params)
{
    if (pname != GL_TEXTURE_ENV_COLOR) {
        glTexEnvf(target, pname, (GLfloat)params[0]);
        return;
    }
    GLfloat color[4];
    for (unsigned i = 0; i < 4; ++i)
        color[i] = (2.0f * (GLfloat)params[i] + 1.0f) / 4294967295.0f;
    glTexEnvfv(target, pname, color);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    glTexParameterf(target, pname, params[0]);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
    glTexParameteri(target, pname, params[0]);
}

void glTexParameterxv(GLenum target, GLenum pname, const GLfixed *params)
{
    glTexParameterx(target, pname, params[0]);
}

/* The host's colour buffer has its first row at the top, where GL puts the
 * bottom, so a projection that is to look right on the screen swaps top and
 * bottom; that mirrors the winding, so the front face becomes clockwise.
 * Both helpers leave GL_MODELVIEW as the current matrix. */
static void finish_projection(void)
{
    glFrontFace(GL_CW);
    glMatrixMode(GL_MODELVIEW);
}

void eapp_gl_perspective(GLfloat fovy_degrees, GLfloat near, GLfloat far)
{
    /* tan(fovy / 2) from the SDK's own table: the angle in 65536ths of a
     * turn, sine over cosine. */
    uint32_t angle = (uint32_t)(fovy_degrees * (65536.0f / 720.0f));
    GLfloat half_height = near * (GLfloat)eapp_sin16(angle) / (GLfloat)eapp_cos16(angle);
    GLfloat half_width = half_height * (GLfloat)EAPP_SCREEN_WIDTH / (GLfloat)EAPP_SCREEN_HEIGHT;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustumf(-half_width, half_width, half_height, -half_height, near, far);
    finish_projection();
}

void eapp_gl_ortho_screen(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(0.0f, (GLfloat)EAPP_SCREEN_WIDTH, (GLfloat)EAPP_SCREEN_HEIGHT, 0.0f, -1.0f, 1.0f);
    finish_projection();
}

/* A new texture of `width` x `height` from `pixels` in the given GL format
 * and type, nearest filtering and clamped edges; 0, or -1 with the texture
 * deleted when GL refused it. */
static int make_texture(GLuint *name, GLsizei width, GLsizei height, GLenum format, GLenum type,
                        const void *pixels)
{
    if (!name)
        return -1;
    while (glGetError() != GL_NO_ERROR)
        ;
    glGenTextures(1, name);
    glBindTexture(GL_TEXTURE_2D, *name);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)format, width, height, 0, format, type, pixels);
    if (glGetError() == GL_NO_ERROR)
        return 0;
    glDeleteTextures(1, name);
    *name = 0;
    return -1;
}

int eapp_gl_texture_from_bitmap(const struct eapp_bitmap *bitmap, GLuint *name)
{
    if (!bitmap || !bitmap->pixels || bitmap->stride_pixels != bitmap->width)
        return -1;
    return make_texture(name, (GLsizei)bitmap->width, (GLsizei)bitmap->height, GL_RGB,
                        GL_UNSIGNED_SHORT_5_6_5, bitmap->pixels);
}

/* The GL format and type of each picture format, in picture.h's order. */
static const struct {
    GLenum format, type;
} gl_formats[EAPP_PICTURE_FORMATS] = {
    {GL_RGB, GL_UNSIGNED_SHORT_5_6_5},      {GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4},
    {GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1},   {GL_RGBA, GL_UNSIGNED_BYTE},
    {GL_ALPHA, GL_UNSIGNED_BYTE},
};

static int picture_texture(const struct eapp_picture_header *header, const void *pixels,
                           GLuint *name, uint32_t *width, uint32_t *height)
{
    int result = make_texture(name, (GLsizei)header->width, (GLsizei)header->height,
                              gl_formats[header->format].format, gl_formats[header->format].type,
                              pixels);
    if (result == 0 && width)
        *width = header->width;
    if (result == 0 && height)
        *height = header->height;
    return result;
}

int eapp_gl_texture_from_file(const char *path, GLuint *name, uint32_t *width,
                              uint32_t *height)
{
    struct eapp_picture_header header;
    int size = eapp_file_read_all(path, &header, sizeof header);
    size_t bytes = size >= (int)sizeof header ? eapp_picture_bytes(&header) : 0;
    if (!bytes || (size_t)size != sizeof header + bytes)
        return -1;
    uint8_t *file = eapp_alloc(sizeof header + bytes);
    if (!file)
        return -1;
    int result = -1;
    if (eapp_file_read_all(path, file, sizeof header + bytes) == size)
        result = picture_texture(&header, file + sizeof header, name, width, height);
    eapp_free(file);
    return result;
}

int eapp_gl_texture_from_pack(const struct eapp_pack *pack, unsigned index, GLuint *name,
                              uint32_t *width, uint32_t *height)
{
    const void *pixels;
    const struct eapp_picture_header *header = eapp_pack_entry(pack, index, &pixels);
    return header ? picture_texture(header, pixels, name, width, height) : -1;
}
