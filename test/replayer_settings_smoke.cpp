#include "audiodecode.h"
#include "replayer_settings.h"
#include "game_music_fixtures.h"
#include "libretro.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <thread>

retro_audio_sample_batch_t audio_batch_cb = [](const int16_t *, size_t n) { return n; };
retro_audio_sample_t audio_cb = nullptr;
using bytes = std::vector<uint8_t>;
using namespace replayer_settings;
static void check(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void save(const std::string &path, const bytes &b) { game_fixtures::save(path, b); }
static bytes imf()
{
    bytes b{0,0,0,0};
    auto reg = [&](unsigned r, unsigned v, unsigned delay = 0) {
        b.insert(b.end(), {uint8_t(r),uint8_t(v),uint8_t(delay),uint8_t(delay >> 8)});
    };
    reg(0x20,1); reg(0x23,1); reg(0x40,0x10); reg(0x43,0);
    reg(0x60,0xf0); reg(0x63,0xf0); reg(0x80,0x77); reg(0x83,0x77);
    reg(0xc0,0); reg(0xa0,0x98); reg(0xb0,0x31,560); reg(0xb0,0x11,140);
    return b;
}
static bytes mod()
{
    bytes b(1084+1024+32);
    std::memcpy(b.data(),"UADE test",9);
    b[43] = 16; b[45] = 64; b[49] = 16; b[950] = 1;
    std::memcpy(b.data()+1080,"M.K.",4);
    b[1084] = 1; b[1085] = 0xac; b[1086] = 0x10;
    b[1102] = 0xb;
    for (unsigned i = 0; i < 32; ++i) b[2108+i] = i < 16 ? 96 : uint8_t(-96);
    return b;
}
static std::vector<uint8_t> sid()
{
    std::vector<uint8_t> b(124);
    memcpy(b.data(), "PSID", 4);
    b[5] = 2;
    b[7] = 124;
    b[8] = 0x10;
    b[10] = 0x10;
    b[12] = 0x10;
    b[13] = 31;
    b[15] = 2;
    b[17] = 1;
    memcpy(b.data() + 22, "Detonate SID fixture", 20);
    const uint8_t code[] = {0xa9, 15, 0x8d, 0x18, 0xd4, 0xa9, 0, 0x8d, 0, 0xd4, 0xa9, 32, 0x8d, 1, 0xd4,
                            0xa9, 0, 0x8d, 5, 0xd4, 0xa9, 0xf0, 0x8d, 6, 0xd4, 0xa9, 0x21, 0x8d, 4, 0xd4, 0x60, 0xee, 1, 0xd4, 0x60};
    b.insert(b.end(), std::begin(code), std::end(code));
    return b;
}
static void be(bytes &b, size_t p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[p + i] = uint8_t(v >> (24 - 8 * i));
}
// One four-channel pattern, a sustained synthesized note and a native loop.
// SMOD uses its built-in wave; FC14 stores a square wave after the sequences.
static bytes module(bool old) {
    const unsigned header = old ? 100 : 180, pattern = header + 13;
    const unsigned sound = pattern + 64, volume = sound + 64, wave = volume + 64;
    bytes b(wave + (old ? 0 : 32));
    std::memcpy(b.data(), old ? "SMOD" : "FC14", 4);
    be(b, 4, 13); be(b, 8, pattern); be(b, 12, 64);
    be(b, 16, sound); be(b, 20, 64); be(b, 24, volume); be(b, 28, 64);
    be(b, 32, wave); b[header + 12] = 3;
    b[header + 4] = 12; // Transpose one channel so the stereo fixture is asymmetric.
    b[pattern] = 24;
    b[sound] = 0xe2; b[sound + 1] = 10; b[sound + 3] = 0xe1;
    b[volume] = 1; b[volume + 5] = 64; b[volume + 6] = 0xe1;
    if (!old) {
        be(b, 36, wave); b[100] = 16;
        for (unsigned i = 0; i < 32; ++i) b[wave + i] = i < 16 ? 64 : 192;
    }
    return b;
}

static std::vector<float> render(auddecode &d)
{
    float *pcm; unsigned n = 16384; d.mix(pcm, n);
    check(n == 16384, "complete configured PCM block");
    std::vector<float> result(pcm, pcm + n * 2);
    double energy = 0;
    for (auto f : result) { check(std::isfinite(f), "finite configured PCM"); energy += std::abs(f); }
    check(energy > .01, "audible configured PCM");
    return result;
}
static bytes audible_nsf()
{
    auto b = game_fixtures::nsf(); b.resize(0x80);
    for (const auto &[reg, value] : {std::pair{0x4015, 1}, {0x4000, 0xbf}, {0x4002, 0x80}, {0x4003, 0x08}})
        b.insert(b.end(), {0xa9, uint8_t(value), 0x8d, uint8_t(reg), uint8_t(reg >> 8)});
    b.push_back(0x60);
    game_fixtures::put(b, 12, 0x8000 + b.size() - 0x80, 2);
    b.push_back(0x60);
    return b;
}
static void control(auddecode *(*factory)(), const char *path, const bytes &b, id option, int value, bool memory = true)
{
    defaults();
    std::unique_ptr<auddecode> d(factory()); float rate;
    if (!memory) save(path, b);
    check(memory ? d->open_memory(path, b, &rate, true) : d->open(path, &rate, true), "open control fixture");
    auto original = render(*d);
    set(option, value);
    check(d->seek(0), "restart applies settings");
    auto changed = render(*d);
    check(changed != original, "native option changes PCM");
    std::unique_ptr<auddecode> fresh(factory());
    check(memory ? fresh->open_memory(path, b, &rate, true) : fresh->open(path, &rate, true), "fresh configured decoder");
    auto loaded = render(*fresh);
    const auto equivalent = [&](const auto &a, const auto &b) {
        // reSIDfp's analogue filter adds dither from a shared noise sequence.
        const float tolerance = factory == create_sid ? 8.f / 32768 : 0;
        float peak = 0;
        for (size_t i = 0; i < a.size(); ++i) peak = std::max(peak, std::abs(a[i] - b[i]));
        if (peak > tolerance) std::fprintf(stderr, "%s: PCM difference %.6f\n", path, peak);
        return peak <= tolerance;
    };
    check(equivalent(changed, loaded), "restart and fresh load apply the same settings");
    defaults();
    check(d->seek(0), "restart with restored defaults");
    check(equivalent(render(*d), original), "restoring defaults restores PCM");
    d.reset(); fresh.reset();
    if (!memory) std::filesystem::remove(path);
    std::printf("%s option %d passed\n", path, int(option));
}
int main(int argc, char **argv)
{
    check(argc == 2, "source root argument");
    const std::filesystem::path config = "replayer-settings-smoke.ini";
    std::filesystem::remove(config);
    std::string error;
    check(load(config, error), "missing defaults file is allowed");
    const auto native = snapshot();
    const auto unavailable = std::filesystem::current_path() / "settings-missing-directory" / "defaults.ini";
    check(load(unavailable, error) && !save(error) && !error.empty() && file_path() == unavailable, "failed save reports error at selected destination");
    check(load(config, error), "restore valid defaults destination");
    set(mpt_ramping, 5); set(pre_interpolation, 1); set(sid_model, 2);
    const auto saved = snapshot();
    check(save(error), "save defaults");
    defaults(); check(snapshot() == native, "restore defaults");
    check(load(config, error) && snapshot() == saved, "saved settings round trip");
    {
        std::ofstream out(config);
        out << "unknown.option=1\nopenmpt.interpolation=999\nopenmpt.ramping=-999\nsid.model=garbage\nuade.filter=1trailing\n";
    }
    check(load(config, error), "load malformed settings safely");
    check(snapshot()[mpt_interpolation] == 4 && snapshot()[mpt_ramping] == -1 && snapshot()[sid_model] == 0 && snapshot()[uade_filter] == 0, "range validation and malformed entries");
    defaults();
    // The playback and browser workers read snapshots while controls are edited.
    std::thread reader([] {
        for (unsigned n = 0; n < 10000; ++n) {
            auto s = snapshot();
            for (const auto &o : options()) check(s[o.setting] >= o.minimum && s[o.setting] <= o.maximum, "thread-safe valid snapshot");
        }
    });
    for (int n = 0; n < 10000; ++n) set(mpt_interpolation, n % 7 - 1);
    reader.join(); defaults();
    auto m = mod();
    control(create_openmpt, "settings.mod", m, mpt_interpolation, 1);
    control(create_openmpt, "settings.mod", m, mpt_gain, -6);
    control(create_openmpt, "settings.mod", m, mpt_ramping, 10);
    control(create_openmpt, "settings.mod", m, mpt_separation, 0);
    std::ifstream in(std::string(argv[1]) + "/test/fixtures/retrovert_selftest.prt", std::ios::binary);
    bytes prt{std::istreambuf_iterator<char>(in), {}};
    control(create_pretracker, "settings.prt", prt, pre_interpolation, 1);
    control(create_pretracker, "settings.prt", prt, pre_mix, 100);
    control(create_pretracker, "settings.prt", prt, pre_delay, 1400);
    control(create_adlib, "settings.imf", imf(), adlib_surround, 1);
    control(create_uade, "settings-uade.mod", m, uade_resampler, 2, false);
    control(create_uade, "settings-uade.mod", m, uade_filter, 2, false);
    control(create_uade, "settings-uade.mod", m, uade_led, 2, false);
    control(create_sid, "settings.sid", sid(), sid_model, 2, false);
    control(create_sid, "settings.sid", sid(), sid_sampling, 1, false);
    control(create_futurecomposer, "settings.fc14", module(false), fc_separation, 0);
    control(create_futurecomposer, "settings.fc14", module(false), fc_filter, 1);
    // GME uses native EQ again when the override is switched off.
    control(create_gme, "settings.nsf", audible_nsf(), gme_eq, 1, false);
    // The same asynchronous restart used by the menu must retain pause/repeat.
    save("settings-player.mod", m);
    music_repeat(true);
    check(music_play("settings-player.mod"), "player opens settings fixture");
    music_pause(true);
    set(mpt_interpolation, 1);
    music_setposition_async(0); music_wait(); music_poll();
    check(music_ispaused() && music_islooping() && music_getposition() == 0 && music_error().empty(), "settings restart retains playback state");
    music_stop(); music_repeat(false); defaults();
    std::filesystem::remove("settings-player.mod");
    std::filesystem::remove(config);
    std::puts("Replayer settings checks passed.");
}
