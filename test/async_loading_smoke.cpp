#include "audiodecode.h"
#include "archive_reader.h"
#include "visualization.h"
#include "libretro.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace {
std::mutex gate_mutex;
std::condition_variable gate;
bool blocked = false, entered = false;
unsigned opens = 0, audio_frames = 0;
const auto frontend = std::this_thread::get_id();
void check(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::abort(); }
}
void arm()
{
    std::lock_guard lock(gate_mutex);
    entered = false; blocked = true;
}
void await_open()
{
    std::unique_lock lock(gate_mutex);
    check(gate.wait_for(lock, std::chrono::seconds(5), [] { return entered; }), "worker starts");
}
void unblock()
{
    std::lock_guard lock(gate_mutex);
    blocked = false; gate.notify_all();
}
struct slow_decoder : auddecode {
    std::string title;
    bool playing = false;
    bool open(const char *name, float *rate, bool) override
    {
        check(std::this_thread::get_id() != frontend, "decoder opens off frontend thread");
        std::unique_lock lock(gate_mutex);
        ++opens; entered = true; gate.notify_all();
        gate.wait(lock, [] { return !blocked; });
        title = name; *rate = 44100; playing = true;
        return true;
    }
    bool open_memory(const std::string &name, const std::vector<uint8_t> &bytes, float *rate, bool loop) override
    {
        const auto result = open(name.c_str(), rate, loop);
        check(bytes == std::vector<uint8_t>({1, 2, 3}), "archive bytes retained until worker completes");
        return result;
    }
    bool seek(unsigned) override { return true; }
    void stop() override { playing = false; }
    bool is_playing() override { return playing; }
    unsigned song_duration() override { return 1000; }
    const char *song_title() override { return title.c_str(); }
    std::vector<std::string> file_types() override { return {"slow"}; }
    void mix(float *&samples, unsigned &frames) override { samples = nullptr; frames = 0; }
};
size_t batch(const int16_t *pcm, size_t frames)
{
    check(std::this_thread::get_id() == frontend, "libretro callbacks stay on frontend");
    for (size_t i = 0; i < frames * 2; ++i) check(pcm[i] == 0, "silence while loader owns decoder");
    audio_frames += unsigned(frames);
    return frames;
}
}
retro_audio_sample_batch_t audio_batch_cb = batch;
retro_audio_sample_t audio_cb = nullptr;
#define FACTORY(name) auddecode *create_##name() { return new slow_decoder; }
FACTORY(common) FACTORY(wav) FACTORY(mpc) FACTORY(wv) FACTORY(vgm) FACTORY(gme)
FACTORY(sid) FACTORY(v2m) FACTORY(organya) FACTORY(klystrack) FACTORY(tfmx) FACTORY(hively) FACTORY(futurecomposer) FACTORY(ken)
FACTORY(psx) FACTORY(sega) FACTORY(qsf) FACTORY(usf) FACTORY(gsf) FACTORY(snsf) FACTORY(2sf)

int main()
{
    arm(); music_play_async("first.slow"); await_open();
    for (unsigned i = 0; i < 30; ++i)
    {
        music_poll(); music_run();
        check(music_loading() && music_title().empty() && music_trackcount() == 0, "UI reads only frontend-owned state");
        (void)music_visualization();
    }
    check(audio_frames == 30 * 735, "frontend continues producing audio while disk is blocked");
    music_play_async("obsolete.slow"); music_play_async("latest.slow");
    music_repeat(true); music_pause(true);
    unblock(); music_wait();
    check(opens == 2 && music_title() == "latest.slow", "only latest queued load becomes active");
    check(music_getrepeat() && music_ispaused(), "pending Repeat and Pause preserved");

    arm(); music_play_async("cancelled.slow"); await_open();
    music_stop_async();
    music_run(); check(music_loading() && music_title().empty(), "Stop returns without joining slow load");
    unblock(); music_wait();
    check(!music_isplaying() && music_title().empty(), "cancelled load cannot restart playback");

    auto members = std::make_shared<std::vector<archive_member>>();
    members->push_back({"archive.slow", {1, 2, 3}});
    std::weak_ptr<const std::vector<archive_member>> lifetime = members;
    arm(); music_play_archive_async(members, 0); members.reset(); await_open();
    check(!lifetime.expired(), "navigation may release archive while audio loads");
    music_run(); unblock(); music_wait();
    check(music_title() == "archive.slow" && lifetime.expired(), "memory decoder owns its input after load");
    music_stop();
    std::puts("Background loading, frontend progress, supersession, Stop and archive ownership passed.");
}
