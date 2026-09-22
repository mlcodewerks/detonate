#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "sdl_input.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    void require(bool ok, const char *what)
    {
        if (!ok)
            throw std::runtime_error(std::string(what) + ": " + SDL_GetError());
    }
    struct core_api
    {
        SDL_SharedObject *library = nullptr;
#define CORE_API(X)                                                                             \
    X(retro_api_version)                                                                        \
    X(retro_init)                                                                               \
    X(retro_deinit) X(retro_run)                                                                \
        X(retro_load_game) X(retro_unload_game) X(retro_get_system_av_info)                     \
            X(retro_set_environment) X(retro_set_video_refresh) X(retro_set_audio_sample_batch) \
                X(retro_set_input_poll) X(retro_set_input_state)
#define DECLARE(name) decltype(&::name) name = nullptr;
        CORE_API(DECLARE)
#undef DECLARE
        void open(const char *path)
        {
            library = SDL_LoadObject(path);
            require(library != nullptr, "Unable to load Detonate core");
#define LOAD(name)                                                             \
    name = reinterpret_cast<decltype(name)>(SDL_LoadFunction(library, #name)); \
    require(name != nullptr, #name);
            CORE_API(LOAD)
#undef LOAD
#undef CORE_API
            if (retro_api_version() != RETRO_API_VERSION)
                throw std::runtime_error("Unsupported core API version.");
        }
        ~core_api()
        {
            if (library)
                SDL_UnloadObject(library);
        }
    };
    struct host;
    host *active = nullptr;
    struct host
    {
        core_api core;
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        SDL_Texture *texture = nullptr;
        SDL_AudioStream *audio = nullptr;
        retro_keyboard_callback keyboard{};
        retro_system_av_info av{};
        mouse_input mouse;
        std::array<bool, RETROK_LAST> keys{};
        bool initialized = false, loaded = false, running = true, hidden = false;
        unsigned video_frames = 0;
        uint64_t audio_frames = 0;
        std::string failure, pending_file;

        ~host()
        {
            if (loaded)
                core.retro_unload_game();
            if (initialized)
                core.retro_deinit();
            if (audio)
                SDL_DestroyAudioStream(audio);
            if (texture)
                SDL_DestroyTexture(texture);
            if (renderer)
                SDL_DestroyRenderer(renderer);
            if (window)
                SDL_DestroyWindow(window);
            SDL_ShowCursor();
            active = nullptr;
        }
        static bool environment(unsigned command, void *data)
        {
            switch (command)
            {
            case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
                return true;
            case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
                return *static_cast<retro_pixel_format *>(data) == RETRO_PIXEL_FORMAT_XRGB8888;
            case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
                active->keyboard = *static_cast<retro_keyboard_callback *>(data);
                return true;
            case RETRO_ENVIRONMENT_SHUTDOWN:
                active->running = false;
                return true;
            default:
                return false;
            }
        }
        static void video(const void *pixels, unsigned w, unsigned h, size_t pitch)
        {
            auto &s = *active;
            if (!pixels)
                return;
            if (pixels == RETRO_HW_FRAME_BUFFER_VALID || w != s.av.geometry.base_width || h != s.av.geometry.base_height ||
                pitch < size_t(w) * 4 || pitch > size_t(std::numeric_limits<int>::max()))
            {
                s.failure = "Unsupported core video frame.";
                return;
            }
            if (!SDL_UpdateTexture(s.texture, nullptr, pixels, int(pitch)))
                s.failure = SDL_GetError();
            ++s.video_frames;
        }
        static size_t samples(const int16_t *pcm, size_t frames)
        {
            auto &s = *active;
            const int queued = SDL_GetAudioStreamQueued(s.audio);
            if (queued < 0)
            {
                s.failure = SDL_GetError();
                return 0;
            }
            // Bound latency to 100 ms. The core retains samples not accepted here.
            const int capacity = int(s.av.timing.sample_rate / 10) * 4;
            frames = std::min(frames, size_t(std::max(0, capacity - queued) / 4));
            if (frames && !SDL_PutAudioStreamData(s.audio, pcm, int(frames * 4)))
            {
                s.failure = SDL_GetError();
                return 0;
            }
            s.audio_frames += frames;
            return frames;
        }
        static void poll() { active->mouse.poll(); }
        static int16_t input(unsigned port, unsigned device, unsigned, unsigned id)
        {
            return port == 0 && device == RETRO_DEVICE_MOUSE ? active->mouse.state(id) : 0;
        }
        void release_input()
        {
            mouse.release();
            if (keyboard.callback)
                for (unsigned key = 0; key < keys.size(); ++key)
                    if (keys[key])
                        keyboard.callback(false, key, 0, 0);
            keys.fill(false);
        }
        void sync_pointer()
        {
            float x, y, logical_x, logical_y;
            SDL_GetMouseState(&x, &y);
            if (SDL_RenderCoordinatesFromWindow(renderer, x, y, &logical_x, &logical_y))
                mouse.push(logical_x, logical_y);
        }
        void start(const std::string &library, bool hide)
        {
            active = this;
            hidden = hide;
            core.open(library.c_str());
            core.retro_set_environment(environment);
            core.retro_set_video_refresh(video);
            core.retro_set_audio_sample_batch(samples);
            core.retro_set_input_poll(poll);
            core.retro_set_input_state(input);
            core.retro_init();
            initialized = true;
            core.retro_get_system_av_info(&av);
            if (!av.geometry.base_width || av.geometry.base_width > 4096 || !av.geometry.base_height || av.geometry.base_height > 4096 ||
                !std::isfinite(av.timing.fps) || av.timing.fps < 1 || av.timing.fps > 240 ||
                !std::isfinite(av.timing.sample_rate) || av.timing.sample_rate < 8000 || av.timing.sample_rate > 384000)
                throw std::runtime_error("Unsupported core geometry or audio timing.");
            window = SDL_CreateWindow("Detonate", int(av.geometry.base_width), int(av.geometry.base_height),
                                      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (hidden ? SDL_WINDOW_HIDDEN : 0));
            require(window != nullptr, "Create window");
            renderer = SDL_CreateRenderer(window, nullptr);
            require(renderer != nullptr, "Create renderer");
            require(SDL_SetRenderLogicalPresentation(renderer, int(av.geometry.base_width), int(av.geometry.base_height), SDL_LOGICAL_PRESENTATION_LETTERBOX), "Set logical resolution");
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, int(av.geometry.base_width), int(av.geometry.base_height));
            require(texture != nullptr, "Create video texture");
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
            const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, int(av.timing.sample_rate)};
            audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            require(audio != nullptr, "Open audio device");
            require(SDL_ResumeAudioStreamDevice(audio), "Start audio device");
            require(SDL_StartTextInput(window), "Start text input");
            if (SDL_GetMouseFocus() == window)
                SDL_HideCursor();
        }
        bool load(const std::string &path)
        {
            release_input();
            if (loaded)
                core.retro_unload_game();
            loaded = false;
            require(SDL_ClearAudioStream(audio), "Clear previous audio");
            mouse.reset(int(av.geometry.base_width), int(av.geometry.base_height));
            // SDL and the libretro core both use UTF-8 paths, including on Windows.
            const retro_game_info game{path.c_str(), nullptr, 0, nullptr};
            loaded = core.retro_load_game(path.empty() ? nullptr : &game);
            const bool opened = loaded;
            if (!loaded)
                loaded = core.retro_load_game(nullptr); // Keep the browser available after a failed drop.
            if (!loaded)
                throw std::runtime_error("Unable to initialize the Detonate browser.");
            SDL_SetWindowTitle(window, path.empty() || !opened ? "Detonate" : ("Detonate - " + path).c_str());
            // Re-align the software cursor after the core resets it on load.
            sync_pointer();
            return opened;
        }
        void events()
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                switch (event.type)
                {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    running = false;
                    break;
                case SDL_EVENT_DROP_FILE:
                    if (event.drop.data)
                        pending_file = event.drop.data;
                    break;
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    release_input();
                    SDL_ShowCursor();
                    break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    if (SDL_GetMouseFocus() == window)
                        SDL_HideCursor();
                    sync_pointer();
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    sync_pointer();
                    break;
                case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                    SDL_ShowCursor();
                    break;
                case SDL_EVENT_WINDOW_MOUSE_ENTER:
                    SDL_HideCursor();
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                {
                    const bool motion = event.type == SDL_EVENT_MOUSE_MOTION;
                    float x, y;
                    if (!SDL_RenderCoordinatesFromWindow(renderer, motion ? event.motion.x : event.button.x, motion ? event.motion.y : event.button.y, &x, &y))
                        break;
                    int button = -1;
                    if (!motion)
                    {
                        if (event.button.button == SDL_BUTTON_LEFT)
                            button = 0;
                        else if (event.button.button == SDL_BUTTON_RIGHT)
                            button = 1;
                        else if (event.button.button == SDL_BUTTON_MIDDLE)
                            button = 2;
                        else
                            break;
                    }
                    mouse.push(x, y, button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                    break;
                }
                case SDL_EVENT_MOUSE_WHEEL:
                    mouse.pending_wheel += event.wheel.y * (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
                    break;
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP:
                {
                    if (event.key.repeat)
                        break;
                    const bool down = event.type == SDL_EVENT_KEY_DOWN;
                    if (event.key.key == SDLK_F11)
                    {
                        if (down)
                            SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
                        break;
                    }
                    if (down && event.key.key == SDLK_Q && (event.key.mod & SDL_KMOD_CTRL))
                    {
                        running = false;
                        break;
                    }
                    const auto key = retro_key(event.key.key);
                    if (key && key < keys.size())
                    {
                        keys[key] = down;
                        if (keyboard.callback)
                            keyboard.callback(down, key, 0, retro_modifiers(event.key.mod));
                    }
                    break;
                }
                case SDL_EVENT_TEXT_INPUT:
                    if (keyboard.callback)
                    {
                        const char *text = event.text.text;
                        while (text && *text)
                            keyboard.callback(true, RETROK_UNKNOWN, SDL_StepUTF8(&text, nullptr), retro_modifiers(SDL_GetModState()));
                    }
                    break;
                }
            }
        }
        void frame()
        {
            core.retro_run();
            if (!failure.empty())
                throw std::runtime_error(failure);
            require(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) && SDL_RenderClear(renderer) &&
                        SDL_RenderTexture(renderer, texture, nullptr, nullptr) && SDL_RenderPresent(renderer),
                    "Present video");
        }
    };
}

int main(int argc, char **argv)
{
    int result = 0;
    try
    {
        std::string library, content;
        unsigned frame_limit = 0;
        bool hidden = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--help" || arg == "-h")
            {
                std::puts("Usage: detonate-sdl3 [--core PATH] [FILE]\nDrop a file to open it. F11: fullscreen. Ctrl+Q: quit.\n--frames N --hidden: bounded/headless smoke runs.");
                return 0;
            }
            if ((arg == "--core" || arg == "--frames") && i + 1 >= argc)
                throw std::runtime_error("Missing option value.");
            if (arg == "--core")
                library = argv[++i];
            else if (arg == "--frames")
            {
                const std::string value = argv[++i];
                size_t used = 0;
                const auto count = std::stoul(value, &used);
                if (used != value.size() || !count || count > 1000000)
                    throw std::runtime_error("Invalid frame count.");
                frame_limit = unsigned(count);
            }
            else if (arg == "--hidden")
                hidden = true;
            else if (arg.starts_with("--"))
                throw std::runtime_error("Unknown option: " + arg);
            else if (content.empty())
                content = arg;
            else
                throw std::runtime_error("Only one content file may be specified.");
        }
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO), "Initialize SDL3");
        if (library.empty())
        {
#if defined(_WIN32)
            const char *name = "detonate-libretro.dll";
#elif defined(__APPLE__)
            const char *name = "detonate-libretro.dylib";
#else
            const char *name = "detonate-libretro.so";
#endif
            const char *base = SDL_GetBasePath();
            require(base != nullptr, "Locate executable");
            library = std::string(base) + name;
        }
        host app;
        app.start(library, hidden);
        if (!app.load(content))
            throw std::runtime_error("Unable to open: " + content);
        const Uint64 interval = Uint64(1000000000.0 / app.av.timing.fps);
        Uint64 deadline = SDL_GetTicksNS();
        unsigned frames = 0;
        while (app.running && (!frame_limit || frames < frame_limit))
        {
            app.events();
            if (!app.running)
                break;
            if (!app.pending_file.empty())
            {
                const auto path = std::move(app.pending_file);
                app.pending_file.clear();
                if (!app.load(path))
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Detonate", ("Unable to open: " + path).c_str(), app.window);
            }
            app.frame();
            ++frames;
            deadline += interval;
            const auto now = SDL_GetTicksNS();
            if (now < deadline)
                SDL_DelayNS(deadline - now);
            else if (now - deadline > interval)
                deadline = now;
        }
        if (frame_limit && (app.video_frames != frames || !app.audio_frames))
            throw std::runtime_error("Core did not deliver video and audio.");
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "Detonate: %s\n", error.what());
        result = 1;
    }
    SDL_Quit();
    return result;
}
