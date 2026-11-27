/* SPDX-License-Identifier: GPL-3.0-only */

/* The gl.h entries gl.c supplies on the host's, with stand-ins for the
 * host's: byte and integer colours scale, enums pass through, vector texture
 * parameters reach the scalar entry. On its own because gl.h's buffer types
 * differ from the symbols header's on a 64-bit host. */

#include <GLES/gl.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eapp_draw.h"
#include "eapp_gl.h"

/* The host's GL entries gl.c builds on: each records its arguments. */
static GLenum gl_target, gl_pname;
static GLfloat gl_floats[4];
static GLint gl_int;
static GLfixed gl_fixed;

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    gl_floats[0] = red;
    gl_floats[1] = green;
    gl_floats[2] = blue;
    gl_floats[3] = alpha;
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    gl_target = target;
    gl_pname = pname;
    gl_floats[0] = param;
}

void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    gl_target = target;
    gl_pname = pname;
    memcpy(gl_floats, params, sizeof gl_floats);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    gl_target = target;
    gl_pname = pname;
    gl_floats[0] = param;
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    gl_target = target;
    gl_pname = pname;
    gl_int = param;
}

void glTexParameterx(GLenum target, GLenum pname, GLfixed param)
{
    gl_target = target;
    gl_pname = pname;
    gl_fixed = param;
}

/* The matrix and texture entries the helpers use, recording their calls. */
static GLenum matrix_mode, front_face, tex_format, tex_type;
static GLfloat frustum[6], ortho[6];
static GLsizei tex_width, tex_height;
static GLenum next_error, image_error; /* image_error is raised by glTexImage2D. */
static unsigned textures_made, textures_deleted, parameters_set;
static const void *tex_pixels;

void glMatrixMode(GLenum mode)
{
    matrix_mode = mode;
}

void glLoadIdentity(void)
{
}

void glFrontFace(GLenum mode)
{
    front_face = mode;
}

void glFrustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f)
{
    GLfloat values[6] = {l, r, b, t, n, f};
    memcpy(frustum, values, sizeof frustum);
}

void glOrthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f)
{
    GLfloat values[6] = {l, r, b, t, n, f};
    memcpy(ortho, values, sizeof ortho);
}

GLenum glGetError(void)
{
    GLenum error = next_error;
    next_error = GL_NO_ERROR;
    return error;
}

void glGenTextures(GLsizei n, GLuint *names)
{
    for (GLsizei i = 0; i < n; ++i)
        names[i] = 7 + textures_made++;
}

void glDeleteTextures(GLsizei n, const GLuint *names)
{
    (void)names;
    textures_deleted += (unsigned)n;
}

void glBindTexture(GLenum target, GLuint name)
{
    assert(target == GL_TEXTURE_2D && name);
}

void glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const void *pixels)
{
    assert(target == GL_TEXTURE_2D && level == 0 && border == 0 && internal == (GLint)format);
    tex_width = width;
    tex_height = height;
    tex_format = format;
    tex_type = type;
    tex_pixels = pixels;
    parameters_set = gl_pname == GL_TEXTURE_WRAP_T;
    next_error = image_error;
    image_error = GL_NO_ERROR;
}

/* The runtime's heap and package files. */
static unsigned blocks;

void *eapp_alloc(size_t size)
{
    ++blocks;
    return malloc(size);
}

void eapp_free(void *pointer)
{
    if (pointer)
        --blocks;
    free(pointer);
}

static const uint8_t glow_file[20 + 4] = {'E', 'I', 'M', 'G', 2, 0, 0, 0, 1,    0,    0,    0,
                                          1,   0,   0,   0,   0, 0, 0, 0, 0x12, 0x34, 0x56, 0x78};

static const uint8_t pack_data[12 + 24] = {'E',  'P',  'A',  'K',  1, 0, 0, 0, 12, 0, 0, 0,
                                           'E',  'I',  'M',  'G',  2, 0, 0, 0, 1,  0, 0, 0,
                                           1,    0,    0,    0,    0, 0, 0, 0, 0x12, 0x34, 0x56, 0x78};

int eapp_file_read_all(const char *path, void *buffer, size_t capacity)
{
    const uint8_t *file = !strcmp(path, "art/glow.eimg") ? glow_file
                          : !strcmp(path, "scene.epak")  ? pack_data
                                                         : NULL;
    size_t size = file == glow_file ? sizeof glow_file : file == pack_data ? sizeof pack_data : 0;
    if (!file)
        return -1;
    memcpy(buffer, file, capacity < size ? capacity : size);
    return (int)size;
}

static void check_helpers(void)
{
    /* A 60 degree field of view at near 1: half height tan(30), width 4:3
     * of it, top and bottom swapped, clockwise front faces. */
    eapp_gl_perspective(60.0f, 1.0f, 10.0f);
    assert(matrix_mode == GL_MODELVIEW && front_face == GL_CW);
    assert(fabsf(frustum[3] + 0.57735f) < 0.002f && fabsf(frustum[2] - 0.57735f) < 0.002f);
    assert(fabsf(frustum[1] - 0.57735f * 4 / 3) < 0.003f && frustum[0] == -frustum[1]);
    assert(frustum[4] == 1.0f && frustum[5] == 10.0f);
    front_face = 0;
    eapp_gl_ortho_screen();
    assert(matrix_mode == GL_MODELVIEW && front_face == GL_CW);
    assert(ortho[0] == 0 && ortho[1] == 320 && ortho[2] == 240 && ortho[3] == 0);

    static const uint16_t pixels[4] = {1, 2, 3, 4};
    struct eapp_bitmap bitmap = {pixels, 2, 2, 2, -1};
    GLuint name = 0;
    assert(eapp_gl_texture_from_bitmap(&bitmap, &name) == 0 && name == 7);
    assert(tex_width == 2 && tex_height == 2 && tex_format == GL_RGB &&
           tex_type == GL_UNSIGNED_SHORT_5_6_5 && tex_pixels == pixels && parameters_set);
    bitmap.stride_pixels = 3;
    assert(eapp_gl_texture_from_bitmap(&bitmap, &name) == -1);
    assert(eapp_gl_texture_from_bitmap(NULL, &name) == -1);
    /* GL refusing the texture deletes it again. */
    bitmap.stride_pixels = 2;
    image_error = GL_INVALID_VALUE;
    assert(eapp_gl_texture_from_bitmap(&bitmap, &name) == -1 && name == 0 &&
           textures_deleted == 1);

    uint32_t width = 0, height = 0;
    assert(eapp_gl_texture_from_file("art/glow.eimg", &name, &width, &height) == 0);
    assert(name && width == 2 && height == 1 && tex_format == GL_RGBA &&
           tex_type == GL_UNSIGNED_SHORT_4_4_4_4 && blocks == 0);
    assert(eapp_gl_texture_from_file("art/none.eimg", &name, NULL, NULL) == -1 && blocks == 0);
    struct eapp_pack *pack = eapp_pack_load("scene.epak");
    assert(pack && eapp_gl_texture_from_pack(pack, 0, &name, &width, &height) == 0);
    assert(width == 2 && height == 1 && tex_type == GL_UNSIGNED_SHORT_4_4_4_4 &&
           !memcmp(tex_pixels, pack_data + 32, 4));
    assert(eapp_gl_texture_from_pack(pack, 1, &name, NULL, NULL) == -1);
    assert(eapp_gl_texture_from_pack(NULL, 0, &name, NULL, NULL) == -1);
    eapp_pack_free(pack);
    assert(blocks == 0);
}

static void check_gl(void)
{
    glColor4ub(255, 0, 51, 128);
    assert(gl_floats[0] == 1.0f && gl_floats[1] == 0.0f && gl_floats[2] == 0.2f &&
           gl_floats[3] > 0.50f && gl_floats[3] < 0.51f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    assert(gl_target == GL_TEXTURE_ENV && gl_pname == GL_TEXTURE_ENV_MODE &&
           gl_floats[0] == (GLfloat)GL_MODULATE);
    static const GLint mode[1] = {GL_DECAL};
    glTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode);
    assert(gl_floats[0] == (GLfloat)GL_DECAL);
    static const GLint color[4] = {0x7fffffff, 0, -0x7fffffff - 1, 0x3fffffff};
    glTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color);
    assert(gl_pname == GL_TEXTURE_ENV_COLOR && gl_floats[0] == 1.0f &&
           gl_floats[1] > 0.0f && gl_floats[1] < 1e-9f && gl_floats[2] == -1.0f &&
           gl_floats[3] > 0.49f && gl_floats[3] < 0.51f);
    static const GLfloat filter_f[2] = {GL_LINEAR, 0};
    static const GLint filter_i[2] = {GL_NEAREST, 0};
    static const GLfixed wrap_x[2] = {GL_REPEAT, 0};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_f);
    assert(gl_target == GL_TEXTURE_2D && gl_pname == GL_TEXTURE_MIN_FILTER &&
           gl_floats[0] == (GLfloat)GL_LINEAR);
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_i);
    assert(gl_pname == GL_TEXTURE_MAG_FILTER && gl_int == GL_NEAREST);
    glTexParameterxv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_x);
    assert(gl_pname == GL_TEXTURE_WRAP_S && gl_fixed == GL_REPEAT);
}

int main(void)
{
    check_gl();
    check_helpers();
    printf("GL forwarders pass\n");
    return 0;
}
