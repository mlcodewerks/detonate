#include "audiodecode.h"
#include "archive_reader.h"
#include "libretro.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <algorithm>

retro_audio_sample_batch_t audio_batch_cb = [](const int16_t *, size_t n) { return n; };
retro_audio_sample_t audio_cb = nullptr;
using bytes = std::vector<uint8_t>;
static void check(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr,"FAIL: %s (%s)\n",message,music_error().c_str()); std::exit(1); }
}
static void save(const std::string &p, const bytes &b)
{
    std::ofstream f(std::filesystem::u8path(p),std::ios::binary);
    f.write(reinterpret_cast<const char *>(b.data()),b.size()); check(bool(f),"write fixture");
}
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
static bytes two_prt_subsongs(const bytes &old)
{
    // Convert the audible old-format fixture to two version 1.5 subsongs.
    bytes b(0x16a);
    std::copy_n(old.begin(),0x5a,b.begin());
    auto be = [&](unsigned p, uint32_t n) {
        for (unsigned i = 0; i < 4; ++i) b[p+i] = uint8_t(n >> (24-i*8));
    };
    b[3] = 0x1e;
    be(4,0x60); be(8,0x80); be(12,0xe0); be(16,0x128); b[0x5a] = 2;
    b[0x61] = b[0x63] = b[0x69] = b[0x6b] = 1;
    b[0x62] = b[0x6a] = 16;
    be(0x6c,48);
    std::copy_n(old.begin()+0x5a,8,b.begin()+0x70);
    std::copy_n(old.begin()+0x5a,8,b.begin()+0x78);
    std::copy_n(old.begin()+0x62,48,b.begin()+0x80);
    std::copy_n(old.begin()+0x62,48,b.begin()+0xb0);
    std::copy_n(old.begin()+0x92,40,b.begin()+0x100);
    std::copy(old.begin()+0xba,old.end(),b.begin()+0x128);
    return b;
}
static std::vector<float> render(auddecode &d, unsigned frames)
{
    float *pcm = nullptr; unsigned n = frames; d.mix(pcm,n);
    check(n == frames,"complete PCM block");
    std::vector<float> result(pcm,pcm+n*2);
    for (float f : result) check(std::isfinite(f),"finite PCM");
    return result;
}
static void verify(auddecode &d, const char *path, bool memory, const bytes &b)
{
    float rate;
    check(memory ? d.open_memory(path,b,&rate,true) : d.open(path,&rate,true),"decoder opens");
    check(rate == 44100 && d.song_duration() > 0 && d.supports_looping(),"rate duration repeat");
    auto first = render(d,44100);
    double energy = 0; for (float f : first) energy += std::abs(f);
    std::printf("%s: tracks %u, duration %u, energy %.3f\n",path,d.track_count(),d.song_duration(),energy);
    check(energy > .01,"audible PCM");
    check(d.seek(0),"seek backwards");
    check(render(d,44100) == first,"deterministic seek to start");
    check(d.seek(100),"seek forwards");
    auto seeked = render(d,512);
    check(std::equal(seeked.begin(),seeked.end(),first.begin()+8820),"exact seek PCM");
    check(!d.select_track(d.track_count()),"invalid subsong rejected");
    check(d.select_track(0),"select first subsong");
}
int main(int argc, char **argv)
{
    check(argc == 2,"source root argument");
    std::string root = argv[1];
    auto prt_path = root + "/test/fixtures/retrovert_selftest.prt";
    std::ifstream f(prt_path,std::ios::binary);
    bytes prt{std::istreambuf_iterator<char>(f),{}};
    auto a = imf(), m = mod();
    save("new-replays.imf",a); save("new-replays.mod",m);
    { std::unique_ptr<auddecode> d(create_adlib()); verify(*d,"new-replays.imf",false,a); }
    { std::unique_ptr<auddecode> d(create_adlib()); verify(*d,"memory.imf",true,a); }
    { std::unique_ptr<auddecode> d(create_pretracker()); verify(*d,prt_path.c_str(),false,prt); }
    { std::unique_ptr<auddecode> d(create_pretracker()); verify(*d,"memory.prt",true,prt); }
    { std::unique_ptr<auddecode> d(create_uade()); verify(*d,"new-replays.mod",false,m); }
    {
        std::unique_ptr<auddecode> d(create_pretracker()); float rate;
        check(d->open_memory("two.prt",two_prt_subsongs(prt),&rate,true),"PRT 1.5 opens");
        check(d->track_count() == 2 && d->select_track(1) && d->current_track() == 1,"PRT 1.5 subsong switch");
        render(*d,8192); check(d->select_track(0),"PRT return to first subsong");
        auto empty = prt; empty[0x41] = 0;
        check(!d->open_memory("empty.prt",empty,&rate,false),"PRT missing waves rejected safely");
    }
    // Two cores must retain independent emulator state across load and destruction.
    for (auto factory : {create_adlib, create_pretracker, create_uade}) {
        const char *path = factory == create_adlib ? "new-replays.imf" : factory == create_pretracker ? prt_path.c_str() : "new-replays.mod";
        std::unique_ptr<auddecode> d(factory()), e(factory()); float rate;
        check(d->open(path,&rate,true),"first instance"); auto reference = render(*d,8192);
        check(e->open(path,&rate,true),"second instance"); check(render(*e,8192) == reference,"independent instances");
        e.reset(); check(d->seek(0),"other instance survives destruction");
        check(render(*d,8192) == reference,"remaining instance retains PCM");
        // A duration boundary must not reset native synthesis when Repeat resumes.
        e.reset(factory()); check(e->open(path,&rate,false),"once instance");
        auto duration = e->song_duration();
        check(duration > 10 && d->seek(duration-10) && e->seek(duration-10),"seek near duration");
        unsigned n = 1024; float *pcm;
        e->mix(pcm,n); check(n > 0 && n < 1024,"bounded first-pass tail");
        render(*d,n);
        n = 1;
        e->mix(pcm,n); check(!n,"first-pass EOF");
        e->set_loop(true);
        check(render(*d,8192) == render(*e,8192),"Repeat resumes native state past duration");
        e->set_loop(false); n = 1; e->mix(pcm,n); check(!n,"Repeat off stops past duration");
    }
    for (const char *name : {"invalid.imf","invalid.prt","invalid.jam","invalid.dm2","jam.invalid","dm2.invalid"}) {
        check(auddecode_supports(name),"extension and Amiga prefix advertised");
        save(name,{'b','a','d'}); float rate;
        check(!std::unique_ptr<auddecode>(make_decoder(name,&rate)),"malformed input rejected");
        std::filesystem::remove(name);
    }
    save("broken.jam",bytes(128,0xff));
    {
        std::unique_ptr<auddecode> d(create_uade()); float rate;
        check(!d->open("broken.jam",&rate,false),"UADE player rejects invalid module body");
    }
    std::filesystem::remove("broken.jam");
    for (auto &[name,b] : std::vector<std::pair<std::string,bytes>>{{"folder/music.imf",a},{"folder/music.prt",prt},{"folder/music.mod",m}}) {
        check(music_play_memory(name,b),"archive memory playback"); music_stop();
    }
    // UADE archive fallback must use its helper as well, rather than OpenMPT.
    save("dm2.tone",m);
    std::vector<archive_member> members{{"folder/dm2.tone",m}};
    check(music_play_memory(members[0].name,m,nullptr,&members),"UADE archive companion fallback"); music_stop();
    std::filesystem::remove("dm2.tone");
    std::filesystem::remove("new-replays.imf"); std::filesystem::remove("new-replays.mod");
    std::puts("AdLib, PreTracker and UADE playback checks passed.");
}
