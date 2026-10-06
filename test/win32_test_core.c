/* Small libretro fixture for testing either loader architecture without
 * rebuilding the decoders. Use real-core tests for actual playback coverage. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "libretro.h"
static retro_environment_t environment;
static retro_video_refresh_t video;
static retro_audio_sample_batch_t audio;
static retro_input_poll_t poll;
static retro_input_state_t input;
static unsigned frames, accepted;
static bool loaded;
static uint32_t pixels[16][20]; /* Deliberately padded rows. */
static int16_t silence[10000];

unsigned retro_api_version(void) { return RETRO_API_VERSION; }
void retro_set_environment(retro_environment_t cb) { environment = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { video = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { poll = cb; }
void retro_set_input_state(retro_input_state_t cb) { input = cb; }
void retro_init(void)
{
    bool no_game = true;
    enum retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environment(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game) ||
        !environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format)) ExitProcess(20);
}
void retro_get_system_av_info(struct retro_system_av_info *av)
{
    av->geometry.base_width = av->geometry.max_width = 16;
    av->geometry.base_height = av->geometry.max_height = 16;
    av->geometry.aspect_ratio = 1;
    av->timing.fps = 60; av->timing.sample_rate = 44100;
}
bool retro_load_game(const struct retro_game_info *game)
{
    if (game && lstrcmpA(game->path, "unicode-\xc3\xa9-\xf0\x9f\x9a\x80.flac")) return false;
    loaded = true;
    return true;
}
void retro_unload_game(void) { loaded = false; }
void retro_run(void)
{
    size_t count;
    if (!loaded || !video || !audio || !poll || !input) ExitProcess(21);
    poll();
    if (input(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X)) ExitProcess(22);
    video(pixels, 16, 16, sizeof(pixels[0]));
    count = audio(silence, 5000); /* Force a partial write with real WinMM. */
    if (count > 4410) ExitProcess(23);
    accepted += (unsigned)count;
    ++frames;
}
void retro_deinit(void)
{
    if (loaded || !frames || !accepted) ExitProcess(24);
}
