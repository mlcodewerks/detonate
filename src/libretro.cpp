#include "libretro.h"
#include "detonate_load.h"
#include "imgui_libretro.h"
#include "imgui_sw.hpp"
#include "audiodecode.h"
#include "player/replayer_settings.h"
#include "archive_reader.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
extern retro_environment_t environ_cb;
#if defined(DETONATE_GL33) || defined(DETONATE_GLES2)
#define DETONATE_HW
#endif
#ifdef DETONATE_GL33
#include "glsym/glsym.h"
#include "glsym/glsym_gl11.h"
#include "imgui_impl_opengl3.h"
#endif
#ifdef DETONATE_GLES2
#include "gles2_renderer.h"
#endif
#ifdef DETONATE_HW
#include "shader_renderer.h"
static retro_hw_render_callback hw{};
static bool hardware = false, gl_ready = false, use_gles2 = false;
static void destroy_renderer(bool lost)
{
    shader_renderer_shutdown(lost);
    if (!gl_ready) return;
#ifdef DETONATE_GLES2
    if (use_gles2) gles2_shutdown(lost);
#endif
#ifdef DETONATE_GL33
    if (!use_gles2)
    {
        if (lost) ImGui_ImplOpenGL3_AbandonDeviceObjects();
        ImGui_ImplOpenGL3_Shutdown();
    }
#endif
    gl_ready = false;
}
static void context_destroy() { destroy_renderer(false); }
static void context_reset()
{
    // Reset without destroy notification means the previous context is gone.
    destroy_renderer(true);
#ifdef DETONATE_GLES2
    if (use_gles2) gl_ready = gles2_init(hw.get_proc_address);
#endif
#ifdef DETONATE_GL33
    if (!use_gles2)
    {
        rglgen_resolve_symbols(hw.get_proc_address);
        if (detonate_gl11_resolve(hw.get_proc_address))
            gl_ready = ImGui_ImplOpenGL3_Init("#version 330 core");
        if (gl_ready && !ImGui_ImplOpenGL3_CreateDeviceObjects())
            context_destroy();
    }
#endif
    if (!gl_ready)
    {
        std::fprintf(stderr, "Detonate: unable to initialize %s renderer.\n", use_gles2 ? "GLES2" : "OpenGL 3.3");
        environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
    }
    else if (!shader_renderer_init(hw.get_proc_address, use_gles2))
        std::fprintf(stderr, "Detonate: shader visualization entry points unavailable.\n");
}
#endif

retro_environment_t environ_cb = nullptr;
retro_video_refresh_t video_cb = nullptr;
retro_audio_sample_t audio_cb = nullptr;
retro_audio_sample_batch_t audio_batch_cb = nullptr;
retro_input_poll_t poller_cb = nullptr;
retro_input_state_t input_state_cb = nullptr;
static constexpr unsigned width = 1280, height = 720;
static bool loaded = false;
static std::array<uint32_t, width * height> framebuffer;
static float mouse_x = width / 2.0f, mouse_y = height / 2.0f;
extern void menus_init(float scale, int width, int height);
extern void menus_run();
extern void menu_request_content(const char *path);
extern int menu_load_status();
extern void menus_wait();
extern void menus_shutdown();

static void shutdown_ui()
{
    menus_shutdown();
    if (!ImGui::GetCurrentContext())
        return;
#ifdef DETONATE_HW
    if (hardware)
        context_destroy();
    else
#endif
        imgui_sw::unbind_imgui_painting();
    ImGui_ImplLibretro_Shutdown();
    ImGui::DestroyContext();
#ifdef DETONATE_HW
    hardware = false;
#endif
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;
    if (!cb)
        return;
    bool no_game = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
    retro_keyboard_callback keyboard = {ImGui_ImpLibretro_ProcessKeys};
    cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &keyboard);
#ifdef DETONATE_HW
    static const retro_variable options[] = {
        {"detonate_renderer", "Renderer (restart required); software"
#ifdef DETONATE_GL33
         "|gl33"
#endif
#ifdef DETONATE_GLES2
         "|gles2"
#endif
        }, {nullptr, nullptr}};
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)options);
#endif
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
    audio_cb = cb;
}
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
}
RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
    poller_cb = cb;
}
RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
}
RETRO_API void retro_init()
{
    const char *directory = nullptr;
    if (environ_cb) environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &directory);
    std::error_code ec;
    auto path = directory && *directory ? std::filesystem::u8path(directory) : std::filesystem::current_path(ec);
    std::string error;
    if (!replayer_settings::load(path / "detonate-replayers.ini", error))
        std::fprintf(stderr, "Detonate: %s\n", error.c_str());
}
RETRO_API void retro_deinit()
{
    menus_wait();
    music_stop();
    shutdown_ui();
    loaded = false;
}
RETRO_API unsigned retro_api_version() { return RETRO_API_VERSION; }
RETRO_API void retro_get_system_info(retro_system_info *info)
{
    static const std::string formats = auddecode_formats() + "|zip|rar|7z";
    *info = {"Detonate", "v2", formats.c_str(), true, true};
}
RETRO_API void retro_get_system_av_info(retro_system_av_info *info)
{
    info->timing = {60.0, 44100.0};
    info->geometry = {width, height, width, height, 16.0f / 9.0f};
}

extern "C" RETRO_API bool detonate_load_game_async(const retro_game_info *game)
{
    if (!environ_cb)
        return false;
    mouse_x = width / 2.0f;
    mouse_y = height / 2.0f;
    if (ImGui::GetCurrentContext())
    {
        ImGui::GetIO().ClearEventsQueue();
        ImGui::GetIO().ClearInputKeys();
        ImGui::GetIO().ClearInputMouse();
    }
    loaded = false;
    retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format))
        return false;
    if (!ImGui::GetCurrentContext())
    {
        menus_init(1.5f, width, height);
        ImGui_ImplLibretro_Init();
#ifdef DETONATE_HW
        retro_variable renderer{"detonate_renderer", nullptr};
        bool request_gl = false;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &renderer) && renderer.value)
        {
            use_gles2 = false;
#ifdef DETONATE_GL33
            request_gl = !std::strcmp(renderer.value, "gl33");
#endif
#ifdef DETONATE_GLES2
            use_gles2 = !std::strcmp(renderer.value, "gles2");
            request_gl = request_gl || use_gles2;
#endif
        }
        if (request_gl)
        {
            hw = {};
            hw.context_type = use_gles2 ? RETRO_HW_CONTEXT_OPENGLES2 : RETRO_HW_CONTEXT_OPENGL_CORE;
            hw.version_major = use_gles2 ? 2 : 3;
            hw.version_minor = use_gles2 ? 0 : 3;
            hw.bottom_left_origin = true;
            hw.context_reset = context_reset;
            hw.context_destroy = context_destroy;
            hardware = environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw);
        }
        if (!hardware)
#endif
            imgui_sw::bind_imgui_painting();
    }
    menu_request_content(game ? game->path : nullptr);
    loaded = true;
    return true;
}
extern "C" RETRO_API int detonate_load_status() { return menu_load_status(); }
RETRO_API bool retro_load_game(const retro_game_info *game)
{
    if (!detonate_load_game_async(game))
        return false;
    menus_wait();
    loaded = menu_load_status() > 0;
    return loaded;
}
RETRO_API void retro_unload_game()
{
    menus_wait();
    music_stop();
    loaded = false;
}
RETRO_API void retro_run()
{
    if (poller_cb)
        poller_cb();
    if (!loaded)
        return;
    if (ImGui::GetCurrentContext())
    {
        if (input_state_cb)
        {

            mouse_x = std::clamp(mouse_x + input_state_cb(0, RETRO_DEVICE_MOUSE, 0,
                                                          RETRO_DEVICE_ID_MOUSE_X),
                                 0.0f, float(width - 1));
            mouse_y = std::clamp(mouse_y + input_state_cb(0, RETRO_DEVICE_MOUSE, 0,
                                                          RETRO_DEVICE_ID_MOUSE_Y),
                                 0.0f, float(height - 1));
            const bool touch = input_state_cb(0, RETRO_DEVICE_POINTER, 0,
                                              RETRO_DEVICE_ID_POINTER_PRESSED) != 0;
            if (touch)
            {
                mouse_x = (input_state_cb(0, RETRO_DEVICE_POINTER, 0,
                                          RETRO_DEVICE_ID_POINTER_X) +
                           32767.0f) *
                          (width - 1) / 65534.0f;
                mouse_y = (input_state_cb(0, RETRO_DEVICE_POINTER, 0,
                                          RETRO_DEVICE_ID_POINTER_Y) +
                           32767.0f) *
                          (height - 1) / 65534.0f;
            }
            const unsigned buttons[] = {RETRO_DEVICE_ID_MOUSE_LEFT,
                                        RETRO_DEVICE_ID_MOUSE_RIGHT,
                                        RETRO_DEVICE_ID_MOUSE_MIDDLE};
            for (int i = 0; i < 3; ++i)
                ImGui_ImplLibretro_ProcessMouse(i, (i == 0 && touch) || input_state_cb(0, RETRO_DEVICE_MOUSE, 0, buttons[i]), mouse_x, mouse_y);
            ImGui_ImplLibretro_ProcessMW(float(input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP) - input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN)));
        }
        else
        {

            for (int i = 0; i < 3; ++i)
                ImGui_ImplLibretro_ProcessMouse(i, false, mouse_x, mouse_y);
        }
#ifdef DETONATE_HW
        if (hardware && !gl_ready)
        {
            if (video_cb)
                video_cb(nullptr, width, height, 0);
            music_run();
            return;
        }
        if (hardware)
        {
#ifdef DETONATE_GLES2
            if (use_gles2) gles2_new_frame();
#endif
#ifdef DETONATE_GL33
            if (!use_gles2) ImGui_ImplOpenGL3_NewFrame();
#endif
        }
#endif
        ImGui_ImplLibretro_NewFrame();
        menus_run();
#ifdef DETONATE_HW
        if (hardware)
        {
#ifdef DETONATE_GLES2
            if (use_gles2) gles2_render(hw.get_current_framebuffer());
#endif
#ifdef DETONATE_GL33
            if (!use_gles2)
            {
                glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)hw.get_current_framebuffer());
                glDisable(GL_SCISSOR_TEST);
                glDisable(GL_FRAMEBUFFER_SRGB);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glClearColor(0, 0, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            }
#endif
            if (video_cb)
                video_cb(RETRO_HW_FRAME_BUFFER_VALID, width, height, 0);
        }
        else
#endif
        {
            framebuffer.fill(0);
            imgui_sw::paint_imgui(framebuffer.data(), ImGui::GetDrawData());
            for (auto &pixel : framebuffer)
                pixel = (((pixel >> IM_COL32_R_SHIFT) & 255u) << 16) | (((pixel >> IM_COL32_G_SHIFT) & 255u) << 8) | ((pixel >> IM_COL32_B_SHIFT) & 255u);
            if (video_cb)
                video_cb(framebuffer.data(), width, height, width * sizeof(uint32_t));
        }
    }
    else if (video_cb)
        video_cb(nullptr, width, height, width * sizeof(uint32_t));
    music_run();
}
RETRO_API void retro_reset() { music_setposition_async(0); }
RETRO_API size_t retro_serialize_size() { return 0; }
RETRO_API bool retro_serialize(void *, size_t) { return false; }
RETRO_API bool retro_unserialize(const void *, size_t) { return false; }
RETRO_API bool retro_load_game_special(unsigned, const retro_game_info *, size_t) { return false; }
RETRO_API unsigned retro_get_region() { return RETRO_REGION_NTSC; }
RETRO_API void *retro_get_memory_data(unsigned) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned) { return 0; }
RETRO_API void retro_cheat_reset() {}
RETRO_API void retro_cheat_set(unsigned, bool, const char *) {}
RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}
