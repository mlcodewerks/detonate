#include "shader_renderer.h"
#include "player/shader_visualization.h"
#ifdef DETONATE_GL33
#include "glsym/rglgen_headers.h"
#else
#include <GLES2/gl2.h>
#endif
#ifndef APIENTRY
#define APIENTRY GL_APIENTRY
#endif
#include <algorithm>
#include <climits>
#include <utility>

namespace
{
#define FUNCTIONS(X) \
    X(GLuint, CreateShader, (GLenum)) X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, CompileShader, (GLuint)) X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, ()) X(void, AttachShader, (GLuint, GLuint)) X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *)) \
    X(void, LinkProgram, (GLuint)) X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) X(void, DeleteProgram, (GLuint)) X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *)) X(void, Uniform1i, (GLint, GLint)) X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform1fv, (GLint, GLsizei, const GLfloat *)) X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
    X(void, Uniform3fv, (GLint, GLsizei, const GLfloat *)) X(void, Uniform4fv, (GLint, GLsizei, const GLfloat *)) \
    X(void, GenBuffers, (GLsizei, GLuint *)) X(void, DeleteBuffers, (GLsizei, const GLuint *)) X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, DisableVertexAttribArray, (GLuint)) X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, GetVertexAttribiv, (GLuint, GLenum, GLint *)) X(void, GetVertexAttribPointerv, (GLuint, GLenum, void **)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei)) X(void, GenTextures, (GLsizei, GLuint *)) X(void, DeleteTextures, (GLsizei, const GLuint *)) \
    X(void, ActiveTexture, (GLenum)) X(void, BindTexture, (GLenum, GLuint)) X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, GenFramebuffers, (GLsizei, GLuint *)) X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, GetIntegerv, (GLenum, GLint *)) X(void, GetBooleanv, (GLenum, GLboolean *)) X(GLboolean, IsEnabled, (GLenum)) \
    X(void, Enable, (GLenum)) X(void, Disable, (GLenum)) X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) X(void, GetFloatv, (GLenum, GLfloat *)) \
    X(void, Clear, (GLbitfield)) X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) X(void, PixelStorei, (GLenum, GLint))
    struct api
    {
#define DECLARE(result, name, args) result (APIENTRY *name) args = nullptr;
        FUNCTIONS(DECLARE)
        DECLARE(void, GenVertexArrays, (GLsizei, GLuint *))
        DECLARE(void, DeleteVertexArrays, (GLsizei, const GLuint *))
        DECLARE(void, BindVertexArray, (GLuint))
        DECLARE(void, BindSampler, (GLuint, GLuint))
#undef DECLARE
    } gl;
    bool es = false, initialized = false;
    constexpr GLenum sampler_binding = 0x8919, vertex_array_binding = 0x85b5, framebuffer_srgb = 0x8db9;
    constexpr GLenum draw_framebuffer = 0x8ca9, read_framebuffer = 0x8ca8, read_framebuffer_binding = 0x8caa;
    constexpr GLenum pixel_unpack_buffer = 0x88ec, pixel_unpack_buffer_binding = 0x88ef;
    constexpr GLenum unpack_row_length = 0x0cf2, unpack_skip_rows = 0x0cf3, unpack_skip_pixels = 0x0cf4;
    struct state_guard
    {
        GLint program, framebuffer, read_fb = 0, buffer, vao = 0, active, viewport[4], unpack;
        GLint textures[8], samplers[8]{}, unpack_buffer = 0, row_length = 0, skip_rows = 0, skip_pixels = 0;
        GLint attrib_enabled = 0, attrib_size = 0, attrib_type = 0, attrib_normalized = 0, attrib_stride = 0, attrib_buffer = 0;
        void *attrib_pointer = nullptr;
        GLboolean mask[4]; GLfloat clear[4];
        const GLenum capabilities[7] = {GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_SCISSOR_TEST, GL_DITHER, framebuffer_srgb};
        GLboolean enabled[7]{};
        state_guard()
        {
            gl.GetIntegerv(GL_CURRENT_PROGRAM, &program); gl.GetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
            gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer); gl.GetIntegerv(GL_ACTIVE_TEXTURE, &active);
            gl.GetIntegerv(GL_VIEWPORT, viewport); gl.GetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
            gl.GetBooleanv(GL_COLOR_WRITEMASK, mask); gl.GetFloatv(GL_COLOR_CLEAR_VALUE, clear);
            for (unsigned i = 0; i < (es ? 6u : 7u); ++i) { enabled[i] = gl.IsEnabled(capabilities[i]); gl.Disable(capabilities[i]); }
            for (unsigned i = 0; i < 8; ++i)
            {
                gl.ActiveTexture(GL_TEXTURE0 + i); gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
                if (!es) { gl.GetIntegerv(sampler_binding, &samplers[i]); gl.BindSampler(i, 0); }
            }
            if (!es)
            {
                gl.GetIntegerv(vertex_array_binding, &vao); gl.GetIntegerv(read_framebuffer_binding, &read_fb);
                gl.GetIntegerv(pixel_unpack_buffer_binding, &unpack_buffer); gl.BindBuffer(pixel_unpack_buffer, 0);
                gl.GetIntegerv(unpack_row_length, &row_length); gl.GetIntegerv(unpack_skip_rows, &skip_rows); gl.GetIntegerv(unpack_skip_pixels, &skip_pixels);
                gl.PixelStorei(unpack_row_length, 0); gl.PixelStorei(unpack_skip_rows, 0); gl.PixelStorei(unpack_skip_pixels, 0);
            }
            else
            {
                gl.GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attrib_enabled);
                gl.GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attrib_size);
                gl.GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &attrib_type);
                gl.GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &attrib_normalized);
                gl.GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &attrib_stride);
                gl.GetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &attrib_buffer);
                gl.GetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &attrib_pointer);
            }
            gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1); gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        ~state_guard()
        {
            gl.UseProgram(program);
            if (!es)
            {
                gl.BindVertexArray(vao); gl.BindFramebuffer(draw_framebuffer, framebuffer); gl.BindFramebuffer(read_framebuffer, read_fb);
                gl.BindBuffer(pixel_unpack_buffer, unpack_buffer); gl.PixelStorei(unpack_row_length, row_length);
                gl.PixelStorei(unpack_skip_rows, skip_rows); gl.PixelStorei(unpack_skip_pixels, skip_pixels);
            }
            else
            {
                gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer); gl.BindBuffer(GL_ARRAY_BUFFER, attrib_buffer);
                gl.VertexAttribPointer(0, attrib_size, attrib_type, attrib_normalized, attrib_stride, attrib_pointer);
                if (attrib_enabled) gl.EnableVertexAttribArray(0); else gl.DisableVertexAttribArray(0);
            }
            gl.BindBuffer(GL_ARRAY_BUFFER, buffer);
            for (unsigned i = 0; i < 8; ++i)
            { gl.ActiveTexture(GL_TEXTURE0 + i); gl.BindTexture(GL_TEXTURE_2D, textures[i]); if (!es) gl.BindSampler(i, samplers[i]); }
            gl.ActiveTexture(active); gl.PixelStorei(GL_UNPACK_ALIGNMENT, unpack);
            gl.Viewport(viewport[0], viewport[1], viewport[2], viewport[3]); gl.ColorMask(mask[0], mask[1], mask[2], mask[3]);
            gl.ClearColor(clear[0], clear[1], clear[2], clear[3]);
            for (unsigned i = 0; i < (es ? 6u : 7u); ++i) if (enabled[i]) gl.Enable(capabilities[i]); else gl.Disable(capabilities[i]);
        }
    };
    struct gpu_pass
    {
        shader_view::pass definition;
        GLuint program = 0, textures[2]{}, fbos[2]{};
        unsigned width = 0, height = 0, latest = 0;
    };
    struct gpu_image { std::string name; GLuint texture = 0; unsigned width, height; };
    struct gpu_graph { std::vector<gpu_pass> passes; std::vector<gpu_image> images; } current;
    GLuint audio[4]{}, vertex_buffer = 0, vertex_array = 0;
    uint64_t attempted_revision = UINT64_MAX;
    GLint frame = 0;
    std::string compile_error;
    void destroy(gpu_graph &g)
    {
        for (auto &p : g.passes) { gl.DeleteProgram(p.program); gl.DeleteTextures(2, p.textures); gl.DeleteFramebuffers(2, p.fbos); }
        for (auto &im : g.images) gl.DeleteTextures(1, &im.texture);
        g = {};
    }
    GLuint texture(unsigned width, unsigned height, const uint8_t *pixels, bool linear = true, bool repeat = false)
    {
        GLuint id; gl.GenTextures(1, &id); gl.BindTexture(GL_TEXTURE_2D, id);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        gl.TexImage2D(GL_TEXTURE_2D, 0, es ? GL_RGBA : 0x8058, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        return id;
    }
    GLuint compile(GLenum type, const std::string &source, std::string &error)
    {
        auto id = gl.CreateShader(type); const char *text = source.c_str();
        gl.ShaderSource(id, 1, &text, nullptr); gl.CompileShader(id);
        GLint ok; gl.GetShaderiv(id, GL_COMPILE_STATUS, &ok);
        if (ok) return id;
        GLint size; gl.GetShaderiv(id, GL_INFO_LOG_LENGTH, &size);
        std::vector<char> log(size > 0 ? size : 1); gl.GetShaderInfoLog(id, GLsizei(log.size()), nullptr, log.data());
        error = log.data(); gl.DeleteShader(id); return 0;
    }
    bool build(const shader_view::graph &graph, gpu_graph &out, std::string &error)
    {
        const auto vertex = compile(GL_VERTEX_SHADER, es ? "#version 100\nattribute vec2 position; void main(){gl_Position=vec4(position,0.0,1.0);}" :
            "#version 330 core\nin vec2 position; void main(){gl_Position=vec4(position,0.0,1.0);}", error);
        if (!vertex) return false;
        for (const auto &pass : graph.passes)
        {
            auto fragment = compile(GL_FRAGMENT_SHADER, shader_view::fragment_source(pass, es), error);
            if (!fragment) { error = pass.name + ": " + error; gl.DeleteShader(vertex); destroy(out); return false; }
            gpu_pass p; p.definition = pass; p.program = gl.CreateProgram();
            gl.AttachShader(p.program, vertex); gl.AttachShader(p.program, fragment); gl.BindAttribLocation(p.program, 0, "position");
            gl.LinkProgram(p.program); gl.DeleteShader(fragment);
            GLint ok; gl.GetProgramiv(p.program, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                GLint size; gl.GetProgramiv(p.program, GL_INFO_LOG_LENGTH, &size); std::vector<char> log(size > 0 ? size : 1);
                gl.GetProgramInfoLog(p.program, GLsizei(log.size()), nullptr, log.data()); error = pass.name + ": " + log.data();
                gl.DeleteProgram(p.program); gl.DeleteShader(vertex); destroy(out); return false;
            }
            out.passes.push_back(std::move(p));
        }
        gl.DeleteShader(vertex);
        GLint limit; gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        gl.ActiveTexture(GL_TEXTURE0);
        for (const auto &im : graph.images)
        {
            if (im.width > limit || im.height > limit) { error = im.name + ": texture exceeds GPU size limit."; destroy(out); return false; }
            out.images.push_back({im.name, texture(im.width, im.height, im.rgba.data(), im.linear, im.repeat), unsigned(im.width), unsigned(im.height)});
        }
        return !out.passes.empty();
    }
    bool resize(gpu_pass &p, unsigned width, unsigned height, std::string &error)
    {
        if (p.width == width && p.height == height) return true;
        gl.DeleteTextures(2, p.textures); gl.DeleteFramebuffers(2, p.fbos);
        p.width = p.height = 0; p.textures[0] = p.textures[1] = p.fbos[0] = p.fbos[1] = 0;
        gl.ActiveTexture(GL_TEXTURE0);
        gl.GenFramebuffers(2, p.fbos);
        for (unsigned i = 0; i < 2; ++i)
        {
            p.textures[i] = texture(width, height, nullptr);
            gl.BindFramebuffer(GL_FRAMEBUFFER, p.fbos[i]);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p.textures[i], 0);
            if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { error = p.definition.name + ": cannot allocate render target."; return false; }
            gl.ClearColor(0, 0, 0, 0); gl.Clear(GL_COLOR_BUFFER_BIT);
        }
        p.width = width; p.height = height; p.latest = 0; return true;
    }
    struct input { GLuint texture; float width, height, time; };
    input resolve(const std::string &name, const gpu_pass &pass, const shader_view::uniforms &u)
    {
        if (name == "audio") return {audio[0], 512, 2, u.audio_time};
        if (name == "fft") return {audio[1], 1024, 1, u.audio_time};
        if (name == "waveform") return {audio[2], 512, 1, u.audio_time};
        if (name == "spectrum") return {audio[3], 32, 1, u.audio_time};
        if (name == "previous") return {pass.textures[pass.latest], float(pass.width), float(pass.height), u.time};
        for (const auto &im : current.images) if (im.name == name) return {im.texture, float(im.width), float(im.height), 0};
        for (const auto &p : current.passes) if (p.definition.name == name) return {p.textures[p.latest], float(p.width), float(p.height), u.time};
        return {0, 0, 0, 0};
    }
    bool render(const shader_view::graph &graph, uint64_t revision, const shader_view::audio_textures &pcm,
                const shader_view::uniforms &u, unsigned width, unsigned height, uintptr_t &output, std::string &error)
    {
        state_guard saved;
        if (revision != attempted_revision)
        {
            attempted_revision = revision;
            gpu_graph next; compile_error.clear();
            if (build(graph, next, compile_error)) { destroy(current); current = std::move(next); frame = 0; }
        }
        error = compile_error;
        if (current.passes.empty()) return false;
        if (!vertex_buffer)
        {
            if (!es) { gl.GenVertexArrays(1, &vertex_array); gl.BindVertexArray(vertex_array); }
            gl.GenBuffers(1, &vertex_buffer); gl.BindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
            constexpr float triangle[] = {-1, -1, 3, -1, -1, 3}; gl.BufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
        }
        if (!es) gl.BindVertexArray(vertex_array);
        gl.BindBuffer(GL_ARRAY_BUFFER, vertex_buffer); gl.EnableVertexAttribArray(0); gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        const uint8_t *pixels[] = {pcm.audio.data(), pcm.fft.data(), pcm.waveform.data(), pcm.spectrum.data()};
        for (unsigned i = 0; i < 4; ++i)
        {
            gl.ActiveTexture(GL_TEXTURE0 + i);
            const unsigned audio_width = i == 3 ? 32 : i == 1 ? 1024 : 512;
            if (!audio[i]) audio[i] = texture(audio_width, i == 0 ? 2 : 1, pixels[i], i != 3);
            else { gl.BindTexture(GL_TEXTURE_2D, audio[i]); gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, audio_width, i == 0 ? 2 : 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels[i]); }
        }
        GLint limit; gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        width = std::clamp(width, 1u, unsigned(std::min(limit, 2048))); height = std::clamp(height, 1u, unsigned(std::min(limit, 2048)));
        for (auto &p : current.passes)
        {
            if (!resize(p, std::max(1u, unsigned(width * p.definition.scale)), std::max(1u, unsigned(height * p.definition.scale)), error)) return false;
            const unsigned target = 1 - p.latest;
            gl.BindFramebuffer(GL_FRAMEBUFFER, p.fbos[target]); gl.Viewport(0, 0, p.width, p.height); gl.UseProgram(p.program);
            auto location = [&](const char *name) { return gl.GetUniformLocation(p.program, name); };
            gl.Uniform3f(location("iResolution"), float(p.width), float(p.height), 1);
            gl.Uniform1f(location("iTime"), u.time); gl.Uniform1f(location("iGlobalTime"), u.time);
            gl.Uniform1f(location("iTimeDelta"), u.delta); gl.Uniform1f(location("iFrameRate"), u.delta > 0 ? 1 / u.delta : 0);
            gl.Uniform1i(location("iFrame"), frame); gl.Uniform1f(location("iSampleRate"), 44100);
            auto mouse = u.mouse;
            for (unsigned i = 0; i < 4; ++i) mouse[i] *= p.definition.scale;
            gl.Uniform4fv(location("iMouse"), 1, mouse.data()); gl.Uniform4fv(location("iDate"), 1, u.date.data());
            float resolutions[12], times[4];
            for (unsigned i = 0; i < 4; ++i)
            {
                const auto in = resolve(p.definition.channels[i], p, u);
                gl.ActiveTexture(GL_TEXTURE0 + i); gl.BindTexture(GL_TEXTURE_2D, in.texture);
                const auto name = "iChannel" + std::to_string(i); gl.Uniform1i(location(name.c_str()), i);
                resolutions[i * 3] = in.width; resolutions[i * 3 + 1] = in.height; resolutions[i * 3 + 2] = 1; times[i] = in.time;
            }
            gl.Uniform3fv(location("iChannelResolution[0]"), 4, resolutions); gl.Uniform1fv(location("iChannelTime[0]"), 4, times);
            const char *aliases[] = {"iFFT", "iWaveform", "iSpectrum", "iAudio"};
            for (unsigned i = 0; i < 4; ++i)
            { gl.ActiveTexture(GL_TEXTURE0 + 4 + i); gl.BindTexture(GL_TEXTURE_2D, audio[(i + 1) % 4]); gl.Uniform1i(location(aliases[i]), 4 + i); }
            gl.DrawArrays(GL_TRIANGLES, 0, 3); p.latest = target;
        }
        if (frame < INT_MAX) ++frame;
        output = current.passes.back().textures[current.passes.back().latest]; return true;
    }
}
bool shader_renderer_init(retro_hw_get_proc_address_t proc, bool es2)
{
    es = es2;
#define LOAD(result, name, args) gl.name = reinterpret_cast<decltype(gl.name)>(proc("gl" #name)); if (!gl.name) return false;
    FUNCTIONS(LOAD)
    if (!es)
    {
        LOAD(void, GenVertexArrays, ()) LOAD(void, DeleteVertexArrays, ()) LOAD(void, BindVertexArray, ()) LOAD(void, BindSampler, ())
    }
#undef LOAD
    initialized = true; shader_view::set_renderer(render); return true;
}
void shader_renderer_shutdown(bool lost)
{
    shader_view::set_renderer(nullptr);
    if (initialized && !lost)
    {
        destroy(current); gl.DeleteTextures(4, audio); gl.DeleteBuffers(1, &vertex_buffer);
        if (!es) gl.DeleteVertexArrays(1, &vertex_array);
    }
    current = {}; std::fill(std::begin(audio), std::end(audio), 0);
    vertex_buffer = vertex_array = 0; attempted_revision = UINT64_MAX; compile_error.clear(); frame = 0; initialized = false;
}
