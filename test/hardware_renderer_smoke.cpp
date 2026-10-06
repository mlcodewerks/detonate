// Exercise the real core and SDL host, including actual GPU pixels and context loss.
#define SDL_MAIN_HANDLED
#define main detonate_sdl_main
#include "standalone/sdl_loader.cpp"
#undef main
#include <vector>

static void check(bool ok, const char *message)
{
    if (!ok) throw std::runtime_error(message);
}

static const char *requested_renderer;
static bool RETRO_CALLCONV reject_gl(unsigned command, void *data)
{
    if (command == RETRO_ENVIRONMENT_GET_VARIABLE)
    {
        static_cast<retro_variable *>(data)->value = requested_renderer;
        return true;
    }
    if (command == RETRO_ENVIRONMENT_SET_HW_RENDER)
        return false;
    return host::environment(command, data);
}

static void check_frame(host &app)
{
    const auto before = app.video_frames;
    for (int i = 0; i < 3; ++i) app.frame();
    check(app.video_frames == before + 3, "Missing hardware video callbacks");
    auto read = reinterpret_cast<void (APIENTRY *)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)>(host::proc("glReadPixels"));
    auto error = reinterpret_cast<GLenum (APIENTRY *)(void)>(host::proc("glGetError"));
    std::vector<unsigned char> pixels(1280 * 720 * 4);
    read(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    unsigned visible = 0, toolbar = 0;
    for (unsigned y = 0; y < 720; ++y)
        for (unsigned x = 0; x < 1280; ++x)
        {
            const auto *p = &pixels[(y * 1280 + x) * 4];
            if (p[0] > 80 || p[1] > 80 || p[2] > 80) ++visible;
            if (y >= 680 && p[0] > 180 && p[1] > 180 && p[2] > 180) ++toolbar;
        }
    check(visible > 1000 && toolbar > 100, "Missing UI/font pixels or inverted GL framebuffer");
    check(error() == GL_NO_ERROR, "OpenGL error while rendering the core");

    for (const auto size : {std::pair{640, 640}, std::pair{1600, 600}})
    {
        check(SDL_SetWindowSize(app.window, size.first, size.second), "Resize window");
        SDL_PumpEvents();
        int w, h;
        SDL_GetWindowSizeInPixels(app.window, &w, &h);
        gl_video_present(&app.gl, w, h, true);
        app.gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        unsigned char pixel[4];
        read(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0, "Letterbox is not black");
        read(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(pixel[0] || pixel[1] || pixel[2], "Blit did not present the core framebuffer");
        app.gl.BindFramebuffer(GL_FRAMEBUFFER, app.gl.framebuffer);
        SDL_GetWindowSize(app.window, &w, &h);
        float x, y;
        check(app.coordinates(w / 2.0f, h / 2.0f, &x, &y) && std::abs(x - 640) < 1 && std::abs(y - 360) < 1,
              "Letterboxed pointer coordinates");
    }
    check(error() == GL_NO_ERROR, "OpenGL error while presenting");
}

int main(int argc, char **argv)
{
    if (argc != 3) return 1;
    requested_renderer = argv[2];
    SDL_SetMainReady();
    try
    {
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO), "Initialize SDL");
        {
            host app;
            app.use_gl = true;
            app.use_gles2 = std::string(argv[2]) == "gles2";
            app.start(argv[1], true);
            auto version = reinterpret_cast<const GLubyte *(APIENTRY *)(GLenum)>(host::proc("glGetString"));
            const auto *version_text = reinterpret_cast<const char *>(version(GL_VERSION));
            check(version_text != nullptr, "Missing GL version");
            if (app.use_gles2)
                check(std::string(version_text).starts_with("OpenGL ES"), "Expected an ES context");
            std::printf("%s: %s\n", argv[2], version_text);
            check(app.load(""), "Load browser");
            app.frame();
            check(app.keyboard.callback != nullptr, "Keyboard callback");
            app.keyboard.callback(true, RETROK_F4, 0, 0);
            app.frame();
            app.keyboard.callback(false, RETROK_F4, 0, 0);
            check_frame(app);
            check(app.load(""), "Reload browser");
            check_frame(app);
            for (bool notify : {true, false})
            {
                if (notify) app.hw.context_destroy();
                gl_video_destroy(&app.gl);
                SDL_GL_DestroyContext(app.context);
                app.context = SDL_GL_CreateContext(app.window);
                require(app.context != nullptr, "Recreate GL context");
                check(gl_video_create(&app.gl, host::proc, 1280, 720, app.use_gles2), "Recreate frontend framebuffer");
                app.hw.context_reset();
                check_frame(app);
            }
        }
        // GL-enabled core still defaults to software when the host does.
        {
            host app;
            app.start(argv[1], true);
            check(app.load(""), "Load software browser");
            app.frame();
            check(app.video_frames == 1, "Software default in GL-enabled build");
        }
        {
            host app;
            app.start(argv[1], true);
            app.core.retro_set_environment(reject_gl);
            check(app.load(""), "Load browser when frontend rejects GL");
            app.frame();
            check(app.video_frames == 1, "Software fallback after rejected GL request");
        }
        SDL_Quit();
        std::puts("Hardware renderer: pixels, orientation, letterboxing, pointer mapping, reload, context loss and software default passed");
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        SDL_Quit();
        return 1;
    }
}
