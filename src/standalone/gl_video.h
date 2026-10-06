/* Small C-compatible GL presenter shared by the CRT-less Win32 and SDL hosts.
 * All entry points come from the host context; there is no GL link dependency. */
#ifndef DETONATE_GL_VIDEO_H
#define DETONATE_GL_VIDEO_H
#ifdef DETONATE_GL33
#include "glsym/rglgen_headers.h"
#else
#include <GLES2/gl2.h>
#endif
#ifndef APIENTRY
#define APIENTRY
#endif

#define HOST_GL_FUNCTIONS(X) \
    X(void, GenFramebuffers, (GLsizei, GLuint *)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, GenTextures, (GLsizei, GLuint *)) \
    X(void, DeleteTextures, (GLsizei, const GLuint *)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, Disable, (GLenum)) \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, Clear, (GLbitfield))

#define HOST_ES_FUNCTIONS(X) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, DeleteProgram, (GLuint)) \
    X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, GenBuffers, (GLsizei, GLuint *)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint *)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))

typedef struct gl_video
{
#define DECLARE(result, name, args) result (APIENTRY *name) args;
    HOST_GL_FUNCTIONS(DECLARE)
    HOST_ES_FUNCTIONS(DECLARE)
    DECLARE(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))
#undef DECLARE
    GLuint framebuffer, texture;
    unsigned width, height;
    bool es2;
    GLuint program, vbo;
    GLint flip, sampler;
} gl_video;

static bool gl_video_create(gl_video *v, retro_hw_get_proc_address_t proc, unsigned width, unsigned height, bool es2)
{
#define LOAD(result, name, args) \
    v->name = (result (APIENTRY *) args)proc("gl" #name); \
    if (!v->name) return false;
    HOST_GL_FUNCTIONS(LOAD)
    if (es2) { HOST_ES_FUNCTIONS(LOAD) }
    else { LOAD(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) }
#undef LOAD
    v->es2 = es2;
    v->width = width;
    v->height = height;
    v->GenTextures(1, &v->texture);
    v->BindTexture(GL_TEXTURE_2D, v->texture);
    v->TexImage2D(GL_TEXTURE_2D, 0, es2 ? GL_RGBA : 0x8058 /* RGBA8 */, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    v->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    v->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    v->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    v->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    v->GenFramebuffers(1, &v->framebuffer);
    v->BindFramebuffer(GL_FRAMEBUFFER, v->framebuffer);
    v->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, v->texture, 0);
    if (v->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    if (es2)
    {
        const char *vs = "#version 100\nattribute vec4 vertex; varying mediump vec2 uv; uniform float flip; void main(){gl_Position=vec4(vertex.xy,0.,1.);uv=vec2(vertex.z,mix(vertex.w,1.-vertex.w,flip));}";
        const char *fs = "#version 100\nprecision mediump float; varying mediump vec2 uv; uniform sampler2D frame; void main(){gl_FragColor=texture2D(frame,uv);}";
        const GLfloat vertices[] = {-1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,1,1,1};
        GLuint vert = v->CreateShader(GL_VERTEX_SHADER), frag = v->CreateShader(GL_FRAGMENT_SHADER);
        GLint ok1 = 0, ok2 = 0;
        v->ShaderSource(vert, 1, &vs, NULL); v->CompileShader(vert);
        v->ShaderSource(frag, 1, &fs, NULL); v->CompileShader(frag);
        v->GetShaderiv(vert, GL_COMPILE_STATUS, &ok1);
        v->GetShaderiv(frag, GL_COMPILE_STATUS, &ok2);
        if (ok1 && ok2)
        {
            v->program = v->CreateProgram();
            v->AttachShader(v->program, vert); v->AttachShader(v->program, frag);
            v->BindAttribLocation(v->program, 0, "vertex");
            v->LinkProgram(v->program);
            v->GetProgramiv(v->program, GL_LINK_STATUS, &ok1);
        }
        v->DeleteShader(vert); v->DeleteShader(frag);
        if (!ok1 || !ok2) return false;
        v->flip = v->GetUniformLocation(v->program, "flip");
        v->sampler = v->GetUniformLocation(v->program, "frame");
        v->GenBuffers(1, &v->vbo);
        v->BindBuffer(GL_ARRAY_BUFFER, v->vbo);
        v->BufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    }
    return true;
}

static void gl_video_destroy(gl_video *v)
{
    if (v->program) v->DeleteProgram(v->program);
    if (v->vbo) v->DeleteBuffers(1, &v->vbo);
    v->program = v->vbo = 0;
    if (v->framebuffer) v->DeleteFramebuffers(1, &v->framebuffer);
    if (v->texture) v->DeleteTextures(1, &v->texture);
    v->framebuffer = v->texture = 0;
}

static void gl_video_present(gl_video *v, int width, int height, bool bottom_left)
{
    int w = width, h = width * (int)v->height / (int)v->width;
    int x, y;
    if (h > height) { h = height; w = height * (int)v->width / (int)v->height; }
    x = (width - w) / 2;
    y = (height - h) / 2;
    v->BindFramebuffer(GL_FRAMEBUFFER, 0);
    v->Disable(GL_SCISSOR_TEST);
#ifdef DETONATE_GL33
    if (!v->es2) v->Disable(GL_FRAMEBUFFER_SRGB);
#endif
    v->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    v->ClearColor(0, 0, 0, 1);
    v->Clear(GL_COLOR_BUFFER_BIT);
    if (v->es2)
    {
        v->Disable(GL_BLEND); v->Disable(GL_DEPTH_TEST); v->Disable(GL_STENCIL_TEST); v->Disable(GL_CULL_FACE);
        v->Viewport(x, y, w, h);
        v->UseProgram(v->program);
        v->Uniform1i(v->sampler, 0); v->Uniform1f(v->flip, bottom_left ? 0.f : 1.f);
        v->ActiveTexture(GL_TEXTURE0); v->BindTexture(GL_TEXTURE_2D, v->texture);
        v->BindBuffer(GL_ARRAY_BUFFER, v->vbo);
        v->EnableVertexAttribArray(0);
        v->VertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), NULL);
        if (w > 0 && h > 0) v->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
#ifdef DETONATE_GL33
    else
    {
        v->BindFramebuffer(GL_READ_FRAMEBUFFER, v->framebuffer);
        if (w > 0 && h > 0)
            v->BlitFramebuffer(0, 0, v->width, v->height, x, bottom_left ? y : y + h,
                               x + w, bottom_left ? y + h : y, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
#endif
    v->BindFramebuffer(GL_FRAMEBUFFER, v->framebuffer);
}
#undef HOST_GL_FUNCTIONS
#undef HOST_ES_FUNCTIONS
#endif
