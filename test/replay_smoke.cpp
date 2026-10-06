#include "audiodecode.h"
#include "archive_reader.h"
#include "formats/xsf_loader.h"
#include "libretro.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <filesystem>

retro_audio_sample_batch_t audio_batch_cb = [](const int16_t *, size_t n)
{ return n; };
retro_audio_sample_t audio_cb = nullptr;
static void check(bool ok, const char *message)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s (%s)\n", message, music_error().c_str());
        std::exit(1);
    }
}
static void save(const char *name, const std::vector<uint8_t> &b)
{
    std::ofstream f(name, std::ios::binary);
    f.write(reinterpret_cast<const char *>(b.data()), b.size());
    check(bool(f), "write fixture");
}
static void le(std::vector<uint8_t> &b, size_t p, uint32_t v, unsigned n = 4)
{
    for (unsigned i = 0; i < n; ++i)
        b[p + i] = uint8_t(v >> (8 * i));
}
static std::vector<uint8_t> org()
{
    std::vector<uint8_t> b(114 + 16);
    memcpy(b.data(), "Org-02", 6);
    le(b, 6, 100, 2);
    b[8] = 4;
    b[9] = 4;
    le(b, 14, 10);
    for (unsigned i = 0; i < 16; ++i)
        le(b, 18 + i * 6, 1000, 2);
    le(b, 22, 2, 2);
    le(b, 114, 0);
    le(b, 118, 5);
    b[122] = 48;
    b[123] = 55;
    b[124] = 5;
    b[125] = 5;
    b[126] = 200;
    b[127] = 200;
    b[128] = 6;
    b[129] = 6;
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
static std::vector<uint8_t> psf(uint8_t version, const std::vector<uint8_t> &exe, const std::string &tags)
{
    uLongf size = compressBound(exe.size());
    std::vector<uint8_t> packed(size);
    check(compress(packed.data(), &size, exe.data(), exe.size()) == Z_OK, "compress PSF");
    packed.resize(size);
    std::vector<uint8_t> b(16);
    memcpy(b.data(), "PSF", 3);
    b[3] = version;
    le(b, 8, packed.size());
    le(b, 12, crc32(0, packed.data(), packed.size()));
    b.insert(b.end(), packed.begin(), packed.end());
    std::string text = "[TAG]" + tags;
    b.insert(b.end(), text.begin(), text.end());
    return b;
}
static void external(const char *path)
{
    std::printf("Checking %s\n", path);
    std::fflush(stdout);
    float rate = 0;
    std::unique_ptr<auddecode> d(make_decoder(path, &rate));
    check(d && rate >= 8000 && rate <= 192000, "decoder opens");
    check(d->supports_looping(), "native repeat supported");
    for (unsigned t = 0; t < d->track_count(); ++t)
    {
        check(d->select_track(t), "select subsong");
        double energy = 0;
        for (unsigned i = 0; i < 200; ++i)
        {
            float *pcm = nullptr;
            unsigned n = 997;
            d->mix(pcm, n);
            for (unsigned j = 0; j < n * 2; ++j)
            {
                check(std::isfinite(pcm[j]), "finite PCM");
                energy += std::abs(pcm[j]);
            }
            if (!n)
                break;
        }
        std::printf("  track %u, rate %.0f, duration %u, energy %.3f\n", t + 1, rate, d->song_duration(), energy);
        std::fflush(stdout);
        check(energy > 0.01, "audible PCM");
        check(d->seek(0) && d->seek(100), "seek backward and forward");
        if (t >= 8)
            break;
    }
    check(d->select_track(0), "return to first subsong");
    auto duration = d->song_duration();
    check(duration > 10, "duration available");
    d->set_loop(true);
    check(d->seek(duration - 10), "seek near duration with Repeat");
    float *pcm = nullptr;
    unsigned n = 4096;
    d->mix(pcm, n);
    check(n == 4096 && d->is_playing(), "continuous PCM past duration");
    d->set_loop(false);
    n = 512;
    d->mix(pcm, n);
    check(!n && !d->is_playing(), "Repeat off stops past duration");
    d->set_loop(true);
    n = 512;
    d->mix(pcm, n);
    check(n == 512 && d->is_playing(), "Repeat resumes after EOF");
    d.reset();
    music_repeat(true);
    check(music_play(path) && music_islooping(), "player native repeat");
    music_setposition(duration - 10);
    for (unsigned i = 0; i < 20; ++i)
        music_run();
    check(music_getposition() > duration && music_isplaying(), "elapsed position crosses duration");
    music_pause(true);
    check(music_settrack(0) && music_ispaused() && music_islooping(), "track change preserves pause and Repeat");
    music_stop();
    music_repeat(false);
}
int main(int argc, char **argv)
{
    try
    {
        if (argc > 1)
        {
            for (int i = 1; i < argc; ++i)
                external(argv[i]);
            return 0;
        }
        for (const char *ext : {"sid", "v2m", "org", "kt", "tfm", "tfx", "mdat", "tfmx", "hvl", "ahx", "psf", "minipsf", "psf2", "minipsf2", "ssf", "minissf", "dsf", "minidsf", "qsf", "miniqsf", "usf", "miniusf", "gsf", "minigsf", "snsf", "minisnsf", "2sf", "mini2sf"})
        {
            auto name = std::string("replay-invalid.") + ext;
            check(auddecode_supports(name.c_str()), "extension advertised");
            save(name.c_str(), {'b', 'a', 'd'});
            check(!music_play(name.c_str()), "reject invalid input");
            std::filesystem::remove(name);
        }
        save("replay-generated.org", org());
        save("replay-generated.sid", sid());
        external("replay-generated.org");
        external("replay-generated.sid");
        // Compare an uninterrupted native run with playback stopped at duration
        // then resumed: Repeat must not reset the engine's oscillator state.
        float rate;
        std::unique_ptr<auddecode> a(make_decoder("replay-generated.org", &rate)), b(make_decoder("replay-generated.org", &rate));
        a->set_loop(true);
        float *pa, *pb;
        unsigned na = 44100, nb = 44100;
        a->mix(pa, na);
        b->mix(pb, nb);
        check(na == nb && std::equal(pa, pa + na * 2, pb), "first pass matches native Repeat");
        nb = 1;
        b->mix(pb, nb);
        check(!nb, "EOF reached");
        b->set_loop(true);
        na = 4096;
        nb = 4096;
        a->mix(pa, na);
        b->mix(pb, nb);
        check(na == nb && std::equal(pa, pa + na * 2, pb), "Repeat after EOF preserves native audio state");
        a.reset();
        b.reset();
        std::vector<uint8_t> exe(12);
        le(exe, 4, 4);
        exe[8] = 1;
        auto lib = psf(0x11, exe, "");
        auto mini = psf(0x11, {}, "_lib=base.ssflib\ntitle=Library test\nlength=0:01.250\n");
        save("base.ssflib", lib);
        save("replay-library.minissf", mini);
        auto loaded = load_xsf("replay-library.minissf");
        check(loaded.sections.size() == 2 && loaded.tags.at("title") == "Library test", "xSF dependency order and metadata");
        save("replay-cycle.minissf", psf(0x11, {}, "_lib=replay-cycle.minissf\n"));
        check(!music_play("replay-cycle.minissf"), "cyclic libraries rejected");
        mini[12] ^= 1;
        save("replay-badcrc.minissf", mini);
        check(!music_play("replay-badcrc.minissf"), "bad CRC rejected");
        std::vector<archive_member> members = {{"folder/song.org", org()}, {"", {'x'}}};
        check(music_play_memory("folder/song.org", members[0].bytes, nullptr, &members), "archive companion tree playback");
        music_stop();
        members.push_back({"../escape", {'x'}});
        check(!music_play_memory("folder/song.org", members[0].bytes, nullptr, &members), "archive traversal rejected");
        for (const char *p : {"replay-generated.org", "replay-generated.sid", "base.ssflib", "replay-library.minissf", "replay-cycle.minissf", "replay-badcrc.minissf"})
            std::filesystem::remove(p);
        std::puts("Replay format, native Repeat and xSF dependency checks passed.");
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
