#include "libretro.h"
#include "detonate_load.h"
#include "game_music_fixtures.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>
#include <chrono>
#include <thread>

static void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}
static retro_keyboard_callback keyboard = {};
static constexpr unsigned width = 1280, height = 720;
static std::vector<uint32_t> pixels(width *height);
static unsigned video_frames = 0;
static unsigned expected_frames = 0;
static bool accept_pixel_format = true;
static unsigned visualization_mode = 1;
static bool pointer_pressed = false;
static int pointer_x = 0, pointer_y = 0;
static uint64_t audio_energy = 0;
static bool environment(unsigned command, void *data)
{
    switch (command)
    {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return accept_pixel_format && *static_cast<retro_pixel_format *>(data) == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
        check(false, "software core must not request a hardware context");
        return false;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        keyboard = *static_cast<retro_keyboard_callback *>(data);
        return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;
    default:
        return false;
    }
}
static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    check(data && data != RETRO_HW_FRAME_BUFFER_VALID, "software framebuffer delivered");
    check(w == width && h == height && pitch == width * sizeof(uint32_t), "software video geometry and pitch");
    for (unsigned y = 0; y < h; ++y)
        std::memcpy(pixels.data() + y * w, static_cast<const unsigned char *>(data) + y * pitch, w * sizeof(uint32_t));
    ++video_frames;
}
static size_t audio(const int16_t *samples, size_t frames)
{
    for (size_t i = 0; i < frames * 2; ++i)
        audio_energy += std::abs(int(samples[i]));
    return frames;
}
static void poll() {}
static int16_t input(unsigned, unsigned device, unsigned, unsigned id)
{
    if (device != RETRO_DEVICE_POINTER)
        return 0;
    if (id == RETRO_DEVICE_ID_POINTER_PRESSED)
        return pointer_pressed;
    if (id == RETRO_DEVICE_ID_POINTER_X)
        return int16_t(pointer_x * 65534 / 1279 - 32767);
    if (id == RETRO_DEVICE_ID_POINTER_Y)
        return int16_t(pointer_y * 65534 / 719 - 32767);
    return 0;
}
static void run_frame()
{
    ++expected_frames;
    retro_run();
    check(video_frames == expected_frames, "one software frame per retro_run");
}
static void run_frames()
{
    for (int i = 0; i < 5; ++i)
        run_frame();
    unsigned visible = 0;
    unsigned chart_background = 0, waveform = 0, toolbar_text = 0;
    for (size_t i = 0; i < pixels.size(); ++i)
    {
        const uint32_t pixel = pixels[i];
        const unsigned r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
        check((pixel & 0xff000000u) == 0, "native-endian XRGB8888 packing");
        if (r > 80 || g > 80 || b > 80)
            ++visible;
        if (pixel == 0x0c121a)
            ++chart_background;
        if (pixel == 0x45ddbb)
            ++waveform;
        if (i / width < 40 && r > 180 && g > 180 && b > 180)
            ++toolbar_text;
    }
    check(visible > 1000, "UI and font texture produce visible pixels");
    check(toolbar_text > 100, "font glyphs render in the top toolbar (top-to-bottom rows)");
    check(visualization_mode ? chart_background > 10000 : chart_background == 0, "view shortcut shows or hides chart");
    check(visualization_mode == 1 ? waveform > 10 : waveform == 0, "oscilloscope and spectrum render distinct views");
    if (std::getenv("DETONATE_SMOKE_SCREENSHOTS"))
    {
        const auto filename = "visualization-" + std::to_string(visualization_mode) + ".ppm";
        auto *file = std::fopen(filename.c_str(), "wb");
        check(file != nullptr, "screenshot file");
        std::fprintf(file, "P6\n%u %u\n255\n", width, height);
        for (uint32_t pixel : pixels)
        {
            const unsigned char rgb[] = {static_cast<unsigned char>(pixel >> 16),
                                         static_cast<unsigned char>(pixel >> 8), static_cast<unsigned char>(pixel)};
            check(std::fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb), "write screenshot");
        }
        check(std::fclose(file) == 0, "close screenshot");
    }
}
static void cycle_visualization()
{
    visualization_mode = (visualization_mode + 1) % 3;
    keyboard.callback(true, RETROK_v, 'v', 0);
    run_frames();
    keyboard.callback(false, RETROK_v, 0, 0);
    run_frames();
}
int main(int argc, char **argv)
{
    check(argc == 2, "source directory argument");
    retro_set_environment(environment);
    retro_set_video_refresh(video);
    retro_set_audio_sample_batch(audio);
    retro_set_input_poll(poll);
    retro_set_input_state(input);
    retro_init();
    keyboard.callback(true, RETROK_UNKNOWN, 'x', 0); // before ImGui exists
    retro_system_info info = {};
    retro_get_system_info(&info);
    check(info.valid_extensions && std::string(info.valid_extensions).find("opus") != std::string::npos, "advertised decoder formats");
    retro_system_av_info av = {};
    retro_get_system_av_info(&av);
    check(av.geometry.base_width == width && av.geometry.base_height == height &&
              av.timing.fps == 60 && av.timing.sample_rate == 44100,
          "software AV information");
    accept_pixel_format = false;
    check(!retro_load_game(nullptr), "reject frontend without XRGB8888");
    retro_run();
    check(video_frames == 0, "failed load does not render");
    accept_pixel_format = true;
    check(retro_load_game(nullptr), "no-content browser load");
    run_frame(); // Rendering starts immediately, without graphics initialization.
    keyboard.callback(true, RETROK_UNKNOWN, 0x03A9, 0);
    keyboard.callback(true, RETROK_LCTRL, 0, RETROKMOD_CTRL);
    keyboard.callback(false, RETROK_LCTRL, 0, 0);
    run_frames();
    retro_reset();
    run_frames();
    retro_unload_game();
    retro_run();
    check(video_frames == expected_frames, "unloaded core does not render");
    const auto path = (std::filesystem::path(argv[1]) / "test" / "test.flac").string();
    retro_game_info game = {path.c_str(), nullptr, 0, nullptr};
    check(retro_load_game(&game), "content reload");
    run_frames();
    pointer_x = 45;
    pointer_y = 25;
    pointer_pressed = true;
    run_frame();
    pointer_pressed = false;
    audio_energy = 0;
    run_frame();
    check(audio_energy == 0, "one-frame pointer click pauses without extra input frames");
    pointer_pressed = true;
    run_frame();
    retro_set_input_state(nullptr);
    audio_energy = 0;
    run_frame();
    check(audio_energy > 0, "removing input releases the held button and completes Resume");
    pointer_pressed = false;
    retro_set_input_state(input);
    cycle_visualization(); // oscilloscope -> spectrum, with active audio
    retro_reset();
    run_frames();          // spectrum survives reset
    cycle_visualization(); // spectrum -> off
    cycle_visualization(); // off -> oscilloscope
    retro_reset();
    check(!retro_serialize(nullptr, 0), "unsupported serialization fails");
    retro_unload_game();
    auto silent_spc = game_fixtures::spc("Silent track");
    silent_spc[0x1010c] = silent_spc[0x1011c] = 0;
    const auto rsn_path = std::filesystem::absolute("generated-ui.rsn");
    game_fixtures::save(rsn_path, game_fixtures::rsn({{"01.spc", game_fixtures::spc("Audible track")}, {"02.spc", silent_spc}}));
    const auto rsn_name = rsn_path.string();
    game = {rsn_name.c_str(), nullptr, 0, nullptr};
    check(retro_load_game(&game), "load RSN in real core");
    audio_energy = 0;
    run_frames();
    check(audio_energy > 0, "RSN initial track produces audio");
    pointer_x = 180;
    pointer_y = 79;
    pointer_pressed = true;
    run_frames();
    pointer_pressed = false;
    run_frames(); // release Next track
    audio_energy = 0;
    run_frames();
    check(audio_energy == 0, "Next track selects silent second SPC and clears queued audio");
    retro_reset();
    audio_energy = 0;
    run_frames();
    check(audio_energy == 0, "selected archive track survives reset");
    retro_unload_game();
    check(info.block_extract && std::string(info.valid_extensions).find("zip") != std::string::npos,
          "frontend leaves archives intact for the browser");
    const auto zip_path = (std::filesystem::path(argv[1]) / "test/fixtures/browser.zip").string();
    game = {zip_path.c_str(), nullptr, 0, nullptr};
    check(retro_load_game(&game), "load ZIP into archive browser");
    audio_energy = 0;
    run_frames();
    check(audio_energy == 0, "archive load waits for song selection");
    const auto double_click = [](int x, int y)
    {
        pointer_x = x;
        pointer_y = y;
        for (int click = 0; click < 2; ++click)
        {
            pointer_pressed = true;
            run_frames();
            pointer_pressed = false;
            run_frames();
        }
    };
    double_click(65, 346); // folder inside ZIP
    double_click(90, 394); // sample.vgz inside folder (after nested.rsn and 01.spc)
    audio_energy = 0;
    run_frames();
    check(audio_energy > 0, "double-click plays VGZ inside ZIP folder");
    retro_reset();
    run_frames();
    pointer_x = 192;
    pointer_y = 25;
    pointer_pressed = true;
    run_frames();
    pointer_pressed = false;
    run_frames(); // open Playback combo (default: Play directory once)
    keyboard.callback(true, RETROK_UP, 0, 0);
    run_frames();
    keyboard.callback(false, RETROK_UP, 0, 0);
    keyboard.callback(true, RETROK_RETURN, 0, 0);
    run_frames();
    keyboard.callback(false, RETROK_RETURN, 0, 0);
    run_frames(); // select Repeat song
    for (int i = 0; i < 17; ++i)
        run_frames();
    audio_energy = 0;
    run_frames();
    check(audio_energy > 0, "Repeat song keeps archived VGZ audible beyond duration");
    retro_unload_game();
    retro_deinit();
    std::filesystem::remove(rsn_path);
    keyboard.callback(false, RETROK_UNKNOWN, 0, 0);
    // Recreate both the ImGui context and its dynamic font atlas in one process.
    retro_init();
    check(retro_load_game(nullptr), "load after full deinitialization");
    run_frames();
    const auto wait_load = []
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        int status;
        do
        {
            run_frame();
            std::this_thread::yield();
            status = detonate_load_status();
            check(std::chrono::steady_clock::now() < deadline, "asynchronous content completion deadline");
        } while (!status);
        return status;
    };
    retro_game_info missing = {"missing-async-file.flac", nullptr, 0, nullptr};
    check(detonate_load_game_async(&missing), "invalid async content request accepted for worker validation");
    check(wait_load() == -1, "async content error reported without closing browser");
    run_frames();
    game = {zip_path.c_str(), nullptr, 0, nullptr};
    check(detonate_load_game_async(&game), "async archive open");
    game = {path.c_str(), nullptr, 0, nullptr};
    check(detonate_load_game_async(&game), "replace pending archive with audio");
    check(wait_load() == 1, "latest content request completes");
    audio_energy = 0; run_frames();
    check(audio_energy > 0, "replacement audio plays after asynchronous load");
    const auto directory = (std::filesystem::path(argv[1]) / "test").string();
    game = {directory.c_str(), nullptr, 0, nullptr};
    check(detonate_load_game_async(&game) && wait_load() == 1, "directory content loads without trying to decode it");
    audio_energy = 0; run_frames();
    check(audio_energy == 0, "directory navigation waits for song selection");
    // Exercise automatic advancement through the real frontend/worker/audio path.
    // Only the middle file is audible, so hearing it proves the first EOF advanced.
    const auto playlist_dir = std::filesystem::absolute("ui-playlist-fixture");
    std::filesystem::create_directory(playlist_dir);
    for (unsigned song = 0; song < 3; ++song)
    {
        game_fixtures::bytes wav(44 + 4410 * 4);
        game_fixtures::text(wav, 0, "RIFF"); game_fixtures::put(wav, 4, uint32_t(wav.size() - 8));
        game_fixtures::text(wav, 8, "WAVEfmt "); game_fixtures::put(wav, 16, 16);
        wav[20] = 1; wav[22] = 2; game_fixtures::put(wav, 24, 44100);
        game_fixtures::put(wav, 28, 176400); wav[32] = 4; wav[34] = 16;
        game_fixtures::text(wav, 36, "data"); game_fixtures::put(wav, 40, 4410 * 4);
        if (song == 1) for (size_t i = 44; i < wav.size(); i += 2) wav[i + 1] = 32;
        game_fixtures::save(playlist_dir / (std::to_string(song) + ".wav"), wav);
    }
    pointer_x = 192; pointer_y = 25; pointer_pressed = true; run_frames();
    pointer_pressed = false; run_frames();
    keyboard.callback(true, RETROK_DOWN, 0, 0); run_frames();
    keyboard.callback(false, RETROK_DOWN, 0, 0);
    keyboard.callback(true, RETROK_RETURN, 0, 0); run_frames();
    keyboard.callback(false, RETROK_RETURN, 0, 0); run_frames(); // Repeat song -> Play directory once
    const auto first_song = (playlist_dir / "0.wav").string();
    game = {first_song.c_str(), nullptr, 0, nullptr};
    check(retro_load_game(&game), "start directory playlist in real core");
    audio_energy = 0;
    for (unsigned i = 0; i < 180; ++i) { run_frame(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    check(audio_energy > 0, "directory playlist advances to audible middle song");
    audio_energy = 0;
    for (unsigned i = 0; i < 120; ++i) run_frame();
    check(audio_energy == 0, "directory playlist stays stopped after final song");
    retro_unload_game();
    for (unsigned song = 0; song < 3; ++song) std::filesystem::remove(playlist_dir / (std::to_string(song) + ".wav"));
    std::filesystem::remove(playlist_dir);
    game = {path.c_str(), nullptr, 0, nullptr};
    check(detonate_load_game_async(&game), "queue content immediately before shutdown");
    retro_deinit(); // also supported without an explicit unload
    keyboard.callback(false, RETROK_UNKNOWN, 0, 0);
    check(video_frames == expected_frames, "all software frames delivered");
    std::puts("Libretro software rendering, input, playback, and reload checks passed.");
    return 0;
}
