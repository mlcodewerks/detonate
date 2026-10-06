#include "shader_renderer.h"
#include "shader_visualization.h"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef DETONATE_GL33
#include "glsym/rglgen_headers.h"
#else
#include <GLES2/gl2.h>
#ifndef APIENTRY
#define APIENTRY GL_APIENTRY
#endif
#endif
#include <cmath>
#include <cstdio>
#include <stdexcept>

static void check(bool ok, const std::string &message)
{ if (!ok) throw std::runtime_error(message); }
static retro_proc_address_t RETRO_CALLCONV proc(const char *name)
{ return reinterpret_cast<retro_proc_address_t>(SDL_GL_GetProcAddress(name)); }
#define FN(result, name, args) auto name = reinterpret_cast<result (APIENTRY *) args>(proc("gl" #name));
int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    const bool es = std::string(argv[1]) == "gles2";
    SDL_Window *window = nullptr; SDL_GLContext context = nullptr;
    try
    {
        SDL_SetMainReady(); check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, es ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, es ? 2 : 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, es ? 0 : 3);
        window = SDL_CreateWindow("Shader GPU test", 128, 128, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        check(window != nullptr, SDL_GetError()); context = SDL_GL_CreateContext(window); check(context != nullptr, SDL_GetError());
        FN(const GLubyte *, GetString, (GLenum)) FN(GLenum, GetError, ()) FN(void, GetIntegerv, (GLenum, GLint *))
        FN(void, GenFramebuffers, (GLsizei, GLuint *)) FN(void, DeleteFramebuffers, (GLsizei, const GLuint *))
        FN(void, BindFramebuffer, (GLenum, GLuint)) FN(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))
        FN(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))
        FN(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) FN(void, Enable, (GLenum))
        FN(GLboolean, IsEnabled, (GLenum)) FN(void, ActiveTexture, (GLenum)) FN(void, BindTexture, (GLenum, GLuint))
        FN(void, GenTextures, (GLsizei, GLuint *)) FN(void, DeleteTextures, (GLsizei, const GLuint *)) FN(GLboolean, IsTexture, (GLuint))
        std::printf("%s: %s\n", argv[1], GetString(GL_VERSION));
        check(shader_renderer_init(proc, es), "Resolve shader renderer");
        GLuint read_fb; GenFramebuffers(1, &read_fb); BindFramebuffer(GL_FRAMEBUFFER, read_fb);
        Viewport(3, 5, 37, 41); Enable(GL_BLEND); Enable(GL_SCISSOR_TEST); ActiveTexture(GL_TEXTURE0 + 3);
        shader_view::uniforms u; u.delta = 1.f / 60;
        visualization_data data; data.left.fill(.5f); data.right.fill(-.5f); data.fft.fill(.8f); data.spectrum.fill(.7f);
        const auto pcm = shader_view::pack_audio(data);
        uint64_t revision = 0; std::string error; uintptr_t output = 0;
        auto render = [&](const shader_view::graph &g, unsigned width = 64, unsigned height = 64) {
            check(shader_view::render(g, revision, pcm, u, width, height, output, error), "Render: " + error);
            GLint viewport[4], framebuffer, active;
            GetIntegerv(GL_VIEWPORT, viewport); GetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer); GetIntegerv(GL_ACTIVE_TEXTURE, &active);
            check(viewport[0] == 3 && viewport[1] == 5 && viewport[2] == 37 && viewport[3] == 41 && GLuint(framebuffer) == read_fb && active == GL_TEXTURE0 + 3 && IsEnabled(GL_BLEND) && IsEnabled(GL_SCISSOR_TEST), "GPU state restored");
            check(GetError() == GL_NO_ERROR, "No GL errors in shader rendering");
        };
        auto pixel = [&](unsigned x = 32, unsigned y = 32) {
            std::array<uint8_t, 4> p; BindFramebuffer(GL_FRAMEBUFFER, read_fb);
            FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, GLuint(output), 0);
            ReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
            check(GetError() == GL_NO_ERROR, "Read shader pixels"); return p;
        };
        shader_view::graph g;
        for (const auto &preset : shader_view::presets())
        {
            check(shader_view::parse(preset.source, {}, g, error), error); ++revision; render(g);
            check(error.empty(), preset.name + std::string(": ") + error);
            auto p = pixel(); check(p[3] == 255 && (p[0] || p[1] || p[2]), "Preset renders visible opaque pixels");
        }
        // Remap every iChannel sampler; named audio aliases must still sample audio.
        check(shader_view::parse("stoy 1\n[pass image]\nchannel0=waveform\nchannel1=spectrum\nchannel2=fft\nchannel3=audio\nshader:\nvoid mainImage(out vec4 c,in vec2 p){c=vec4(texture(iFFT,vec2(.5)).r,texture(iWaveform,vec2(.5)).r,texture(iAudio,vec2(.5,.75)).r,1.0);}\n[end]\n", {}, g, error), error);
        ++revision; render(g); auto p = pixel(); check(std::abs(int(p[0]) - 204) <= 1 && std::abs(int(p[1]) - 191) <= 1 && p[2] == 128, "Audio aliases independent of pass channel bindings");
        check(shader_view::parse("stoy 1\n[pass history]\nscale=0.5\nchannel0=previous\nshader:\nvoid mainImage(out vec4 c,in vec2 p){c=vec4(texture(iChannel0,p/iResolution.xy).r+0.1,0.0,0.0,1.0);}\n[end]\n[pass image]\nchannel0=history\nshader:\nvoid mainImage(out vec4 c,in vec2 p){c=vec4(texture(iChannel0,p/iResolution.xy).r,iChannelResolution[0].x/128.0,float(iFrame)/255.0,1.0);}\n[end]\n", {}, g, error), error);
        ++revision; render(g); p = pixel(); check(std::abs(int(p[0]) - 26) <= 1 && p[1] == 64 && p[2] == 0, "First frame, scaled earlier pass and cleared feedback");
        const int increment = p[0]; // RGBA8 rounding can choose either adjacent byte.
        render(g); p = pixel(); check(std::abs(int(p[0]) - increment * 2) <= 1 && p[2] == 1, "Previous frame feedback and frame uniform");
        auto bad = g; bad.passes.back().source = "this is invalid GLSL";
        ++revision; render(bad); p = pixel(); check(!error.empty() && std::abs(int(p[0]) - increment * 3) <= 2, "Failed compilation keeps entire last good graph");
        ++revision; render(g, 96, 48); p = pixel(); check(error.empty() && std::abs(int(p[0]) - 26) <= 1 && p[1] == 96, "Resize rebuilds targets and clears history");
        // A normal image texture can be sampled like any pass or audio texture.
        g = {}; shader_view::image im; im.name = "color"; im.width = im.height = 1; im.rgba = {20, 90, 180, 255}; g.images.push_back(im);
        shader_view::pass image; image.name = "image"; image.channels[0] = "color";
        image.source = "void mainImage(out vec4 c,in vec2 p){c=texture(iChannel0,p/iResolution.xy);}"; g.passes.push_back(image);
        ++revision; render(g); check(pixel() == std::array<uint8_t, 4>{20, 90, 180, 255}, "Image texture sampling");
        DeleteFramebuffers(1, &read_fb);
        // Lose a context without notification, then ensure old IDs cannot delete new objects.
        SDL_GL_DestroyContext(context); context = SDL_GL_CreateContext(window); check(context != nullptr, SDL_GetError());
        GLuint sentinel; GenTextures(1, &sentinel); BindTexture(GL_TEXTURE_2D, sentinel);
        shader_renderer_shutdown(true); check(IsTexture(sentinel), "Lost-context cleanup preserves new-context objects");
        DeleteTextures(1, &sentinel); check(shader_renderer_init(proc, es), "Reinitialize shaders after context loss");
        GenFramebuffers(1, &read_fb); BindFramebuffer(GL_FRAMEBUFFER, read_fb); Viewport(3, 5, 37, 41); Enable(GL_BLEND); Enable(GL_SCISSOR_TEST); ActiveTexture(GL_TEXTURE0 + 3);
        render(g); check(error.empty() && pixel() == std::array<uint8_t, 4>{20, 90, 180, 255}, "Graph rebuilt after context loss");
        shader_renderer_shutdown(false); DeleteFramebuffers(1, &read_fb);
        SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
        std::puts("Shader GPU checks passed."); return 0;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        if (context) { shader_renderer_shutdown(false); SDL_GL_DestroyContext(context); }
        if (window) SDL_DestroyWindow(window);
        SDL_Quit(); return 1;
    }
}
