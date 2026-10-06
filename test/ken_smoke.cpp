#include "audiodecode.h"
#include "archive_reader.h"
#include "libretro.h"
#include "replays/Ken/backend.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
retro_audio_sample_batch_t audio_batch_cb = [](const int16_t *, size_t n) { return n; };
retro_audio_sample_t audio_cb = nullptr;
using bytes = std::vector<uint8_t>;
static void check(bool ok, const char *s) { if (!ok) { std::fprintf(stderr,"FAIL: %s (%s)\n",s,music_error().c_str()); std::exit(1); } }
static void le(bytes &b, size_t p, uint32_t v) { for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(v>>(i*8)); }
static bytes fixture(const std::string &ext) {
    if(ext=="sm") { bytes b(39); b[0]=100; b[12]=1; b[13]=60; b[25]=78; b[38]=1; return b; }
    if(ext=="snd") return {'V',':',':',':','9',13,10,'X',':',':',':','9',13,10};
    if(ext=="ksm") {
        bytes b(90); b[16]=4; b[32]=1; b[64]=63; b[80]=2;
        le(b,82,(1<<12)|64|24); le(b,86,(241<<12)|24); return b;
    }
    bytes b(38); le(b,4,2); le(b,8,1); b[13]=1; b[14]=b[15]=255;
    le(b,16,0); le(b,20,120); b[26]=b[27]=24; b[28]=b[30]=255; return b;
}
static bytes waves() {
    bytes b(8+32+256); le(b,4,1); le(b,24,256); le(b,32,256);
    for(unsigned i=0;i<256;++i)b[40+i]=i<128?192:64;
    return b;
}
static bytes tag(bytes b) {
    auto p=b.size(); b.resize(p+128); std::memcpy(b.data()+p,"TAGKen song",11);
    std::memcpy(b.data()+p+33,"Ken artist",10); return b;
}
static bytes ape(bytes b) {
    auto p = b.size(); b.resize(p + 8); le(b, p, 8);
    for (char c : std::string("Title")) b.push_back(uint8_t(c));
    b.push_back(0);
    for (char c : std::string("APE song")) b.push_back(uint8_t(c));
    auto footer = b.size(); b.resize(footer + 32);
    std::memcpy(b.data() + footer, "APETAGEX", 8);
    le(b, footer + 8, 2000); le(b, footer + 12, uint32_t(b.size() - p)); le(b, footer + 16, 1);
    return b;
}
static std::vector<float> render(auddecode &d, unsigned frames) {
    float *p=nullptr; auto n=frames; d.mix(p,n); check(n==frames,"complete audio block");
    return {p,p+n*2};
}
int main() {
    for(const std::string ext:{"sm","snd","ksm","kdm"}) {
        std::printf("Checking %s\n",ext.c_str()); std::fflush(stdout);
        check(auddecode_supports(("song."+ext).c_str()),"advertised format");
        auto b=fixture(ext); float rate=0;
        std::unique_ptr<auddecode> a(create_ken()), other(create_ken());
        const auto path=std::filesystem::path("ken-smoke-"+ext); std::filesystem::create_directory(path);
        const auto file=path/std::filesystem::path(u8"\u97f3\u697d."+std::u8string(ext.begin(),ext.end()));
        {std::ofstream f(file,std::ios::binary); auto t=tag(b); f.write((const char*)t.data(),t.size());}
        if(ext=="kdm") {auto w=waves(); std::ofstream f(path/"waves.kwv",std::ios::binary);f.write((const char*)w.data(),w.size());}
        auto u8=file.u8string(); std::string name(u8.begin(),u8.end());
        check(a->open(name.c_str(),&rate,false),"file open");
        check(std::string(a->song_title())=="Ken song" && std::string(a->song_artist())=="Ken artist","tags");
        check(rate==44100 && a->track_count()==1 && !a->select_track(1),"rate and track bounds");
        const auto pcm=render(*a,4410); double energy=0;
        for(auto v:pcm){check(std::isfinite(v),"finite PCM");energy+=std::abs(v);}
        check(energy>0.1,"audible PCM");
        std::thread worker([&]{check(other->open(name.c_str(),&rate,false),"independent worker decoder");render(*other,12345);}); worker.join(); other.reset();
        const auto next=render(*a,997);
        check(a->seek(0) && render(*a,4410)==pcm,"exact rewind and isolated decoder state");
        check(a->seek(100) && render(*a,997)==next,"exact seek");
        auto duration=a->song_duration();check(duration>100 && duration<2000,"duration");
        check(a->seek(duration+1),"seek EOF"); unsigned n=1;float *p=nullptr;a->mix(p,n);check(!n,"nonrepeat EOF");
        a->set_loop(true);render(*a,44100);check(a->is_playing(),"native repeat");
        check(a->open_memory("song."+ext,b,&rate,false)==(ext!="kdm"),"direct memory support / required KDM bank");
        if (ext != "kdm") check(a->open_memory("tagged." + ext, ape(b), &rate, false) && std::string(a->song_title()) == "APE song", "APEv2 metadata");
        auto members=std::make_shared<std::vector<archive_member>>(); members->push_back({"dir/song."+ext,tag(b)});
        if(ext=="kdm")members->push_back({"dir/waves.kwv",waves()});
        music_play_archive_async(members,0);members.reset();music_wait();music_poll();
        check(music_isplaying() && music_title().find("Ken song")!=std::string::npos,"async archive and companion playback");music_stop();
        for(size_t len:{size_t(0),size_t(3),b.size()-1}) {bytes bad(b.begin(),b.begin()+len);check(!a->open_memory("bad."+ext,bad,&rate,false),"reject truncation");}
        std::filesystem::remove(file);if(ext=="kdm")std::filesystem::remove(path/"waves.kwv");std::filesystem::remove(path);
    }
    bytes bad=fixture("ksm");bad[16]=255;float rate;std::unique_ptr<auddecode>d(create_ken());check(!d->open_memory("bad.ksm",bad,&rate,false),"invalid quantization");
    bad=fixture("sm");bad[0]=130;check(!d->open_memory("bad.sm",bad,&rate,false),"invalid SM instrument");
    auto bank = waves(); le(bank, 24, 0x7fffffff);
    check(!ken::load("kdm", fixture("kdm"), bank, 44100), "reject oversized bank sample");
    bank = waves(); le(bank, 28, 255); le(bank, 32, 256);
    check(!ken::load("kdm", fixture("kdm"), bank, 44100), "reject sample loop outside bank");
    bad = fixture("kdm"); bad[24] = 1;
    check(!ken::load("kdm", bad, waves(), 44100), "reject invalid KDM track");
    bad = fixture("kdm"); bad[32] = 17;
    check(!ken::load("kdm", bad, waves(), 44100), "reject invalid KDM effect");
    std::puts("Ken playback, tags, memory, archives, instance isolation, seek and repeat passed");
}
