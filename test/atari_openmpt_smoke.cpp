#include "audiodecode.h"
#include "archive_reader.h"
#include "decoder_base.h"
#include "libretro.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

retro_audio_sample_batch_t audio_batch_cb = [](const int16_t*, size_t n) { return n; };
retro_audio_sample_t audio_cb = nullptr;
static void check(bool ok, const char* msg)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); }
}
static void be(std::vector<uint8_t>& b, size_t p, uint32_t v, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) b[p + i] = uint8_t(v >> ((n - i - 1) * 8));
}
static void text(std::vector<uint8_t>& b, const char* s)
{
    b.insert(b.end(), s, s + std::strlen(s) + 1);
}
static std::vector<uint8_t> ym()
{
    std::vector<uint8_t> b(34);
    std::memcpy(b.data(), "YM6!LeOnArD!", 12);
    be(b, 12, 100, 4);
    be(b, 22, 2000000, 4);
    be(b, 26, 50, 2);
    be(b, 28, 25, 4);
    text(b, "YM fixture"); text(b, "Test author"); text(b, "Synthetic");
    for (unsigned i = 0; i < 100; ++i) {
        size_t p = b.size(); b.resize(p + 16);
        b[p] = uint8_t(180 + i % 20); b[p + 7] = 0x3e; b[p + 8] = 15; b[p + 13] = 0xff;
    }
    b.insert(b.end(), {'E', 'n', 'd', '!'});
    return b;
}
static std::vector<uint8_t> sndh()
{
    std::vector<uint8_t> b(128);
    b[0] = b[4] = b[8] = 0x60;
    be(b, 2, 126, 2);
    std::memcpy(b.data() + 12, "SNDH", 4);
    std::vector<uint8_t> header;
    header.insert(header.end(), {'#', '#', '0', '2'});
    text(header, "!#02"); text(header, "TITLSNDH fixture"); text(header, "COMMTest author"); text(header, "!V50");
    header.insert(header.end(), {'F', 'R', 'M', 'S', 0, 0, 0, 50, 0, 0, 0, 100, 'H', 'D', 'N', 'S'});
    std::copy(header.begin(), header.end(), b.begin() + 16);
    auto write = [&](unsigned reg, unsigned value) {
        b.insert(b.end(), {0x13, 0xfc, 0, uint8_t(reg), 0, 0xff, 0x88, 0,
                          0x13, 0xfc, 0, uint8_t(value), 0, 0xff, 0x88, 2});
    };
    write(0, 200); write(1, 0); write(7, 0x3e); write(8, 15);
    be(b, 6, uint32_t(b.size() - 6), 2);
    be(b, 10, uint32_t(b.size() - 10), 2);
    b.insert(b.end(), {0x4e, 0x75});
    return b;
}
static std::vector<uint8_t> multi_mod()
{
    std::vector<uint8_t> b(1084 + 2048 + 32);
    std::memcpy(b.data(), "Two subsongs", 12);
    be(b, 42, 16, 2); b[45] = 64; be(b, 48, 16, 2);
    b[950] = 2; b[953] = 1;
    std::memcpy(b.data() + 1080, "M.K.", 4);
    for (size_t p : {size_t(1084), size_t(1084 + 1024)}) {
        b[p] = 1; b[p + 1] = 0xac; b[p + 2] = 0x10;
        b[p + 16 + 2] = 0x0b; b[p + 16 + 3] = p == 1084 ? 0 : 1;
    }
    for (size_t p = b.size() - 32; p < b.size(); ++p) b[p] = p & 16 ? 96 : uint8_t(-96);
    return b;
}
static std::vector<uint8_t> lzh(const std::vector<uint8_t>& data)
{
    // Valid LH5 singleton Huffman blocks, one literal per block.
    std::vector<uint8_t> score; unsigned bit = 0;
    auto bits = [&](unsigned value, unsigned n) {
        while (n--) {
            if (!(bit % 8)) score.push_back(0);
            score.back() |= ((value >> n) & 1) << (7 - bit % 8); ++bit;
        }
    };
    for (uint8_t v : data) {
        bits(1, 16); bits(0, 5); bits(0, 5); bits(0, 9); bits(v, 9); bits(0, 4); bits(0, 4);
    }
    std::vector<uint8_t> b(31);
    b[0] = 29; std::memcpy(b.data() + 2, "-lh5-", 5); b[21] = 7;
    std::memcpy(b.data() + 22, "tone.ym", 7);
    for (unsigned i = 0; i < 4; ++i) {
        b[7 + i] = uint8_t(score.size() >> (8 * i)); b[11 + i] = uint8_t(data.size() >> (8 * i));
    }
    b.insert(b.end(), score.begin(), score.end()); b.push_back(0);
    return b;
}
static std::vector<uint8_t> ice(const std::vector<uint8_t>& data)
{
    // A single long literal run with ICE's backwards command-bit stream.
    check(data.size() >= 15 && data.size() < 270, "ICE fixture literal size");
    std::vector<unsigned> stream;
    auto bits = [&](unsigned value, unsigned n) { while (n--) stream.push_back((value >> n) & 1); };
    bits(1, 1); bits(1, 1); bits(3, 2); bits(3, 2); bits(7, 3); bits(unsigned(data.size() - 15), 8);
    std::vector<uint8_t> cmds;
    size_t p = 0;
    while (p < stream.size()) {
        unsigned n = cmds.empty() ? 7 : 8, byte = 0;
        for (unsigned i = 0; i < n; ++i) byte = (byte << 1) | (p < stream.size() ? stream[p++] : 0);
        cmds.push_back(uint8_t(cmds.empty() ? (byte << 1) | 1 : byte));
    }
    std::vector<uint8_t> b(12); std::memcpy(b.data(), "ICE!", 4);
    be(b, 4, uint32_t(12 + data.size() + cmds.size()), 4); be(b, 8, uint32_t(data.size()), 4);
    b.insert(b.end(), data.begin(), data.end()); b.insert(b.end(), cmds.rbegin(), cmds.rend());
    return b;
}
static std::vector<float> render(auddecode& d, unsigned n)
{
    std::vector<float> result;
    while (n) {
        unsigned count = std::min(n, 997u); float* out = nullptr;
        d.mix(out, count);
        check(count && out, "audio block");
        result.insert(result.end(), out, out + count * 2); n -= count;
    }
    return result;
}
static void atari(const std::vector<uint8_t>& bytes, const char* name, bool multi)
{
    float rate = 0;
    std::unique_ptr<auddecode> a(create_atari()), b(create_atari());
    check(a->open_memory(name, bytes, &rate, false) && rate == 44100, "Atari memory open");
    check(b->open_memory(name, bytes, &rate, false), "independent Atari instance");
    check(std::string(a->song_artist()) == "Test author", "Atari artist");
    check(a->track_count() == (multi ? 2u : 1u) && a->current_track() == (multi ? 1u : 0u), "default subsong");
    auto first = render(*a, 4410);
    check(first == render(*b, 4410), "independent instances produce identical PCM");
    double energy = 0;
    for (float v : first) { check(std::isfinite(v), "finite Atari PCM"); energy += std::abs(v); }
    check(energy > 1, "audible Atari tone");
    auto next = render(*a, 997);
    check(b->seek(100) && next == render(*b, 997), "Atari seek matches continuous playback");
    if (multi) {
        check(a->select_track(0), "SNDH switch track");
        unsigned short_duration = a->song_duration();
        check(a->select_track(1) && a->song_duration() > short_duration, "per-subsong duration");
    }
    check(!a->select_track(a->track_count()), "invalid subsong rejected");
    unsigned duration = a->song_duration();
    check(duration > 100 && duration < 2100, "known Atari duration");
    check(a->seek(duration), "seek to Atari EOF");
    unsigned count = 128; float* out = nullptr; a->mix(out, count);
    check(count <= 45 && !a->is_playing(), "Atari nonrepeat EOF (duration rounded to milliseconds)");
    a->set_loop(true); check(render(*a, 4096).size() == 8192, "Atari native repeat beyond duration");
    a->set_loop(false); count = 16; a->mix(out, count);
    check(!count, "Atari live repeat disable");

    auto path = std::filesystem::path(std::u8string(u8"atari-音楽-") + std::u8string(reinterpret_cast<const char8_t*>(name)));
    { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
    auto utf8 = path.u8string();
    std::unique_ptr<auddecode> file(make_decoder(reinterpret_cast<const char*>(utf8.c_str()), &rate));
    check(bool(file), "Unicode Atari file open");
    check(std::filesystem::remove(path), "Atari closes source file");
    check(file->seek(100) && !render(*file, 997).empty(), "buffered Atari seek after file removal");

    auto members = std::make_shared<std::vector<archive_member>>();
    members->push_back({name, bytes});
    check(music_subsongs(name, members, 0).size() == (multi ? 2u : 0u), "archive subsong enumeration");
    music_play_archive_async(members, 0); music_wait();
    check(music_isplaying() && music_trackcount() == (multi ? 2u : 1u), "background archive playback");
    music_run(); music_stop();
}
int main(int argc, char** argv)
{
    check(argc == 2, "source directory argument");
    check(auddecode_supports("song.SNDH") && auddecode_supports("song.YM") && auddecode_supports("song.UMX"), "registered extensions");
    atari(ym(), "tone.ym", false); atari(sndh(), "tone.sndh", true);
    atari(lzh(ym()), "packed.ym", false); atari(ice(sndh()), "packed.sndh", true);
    float rate = 0;
    std::unique_ptr<auddecode> module(create_openmpt());
    for (const char* ext : {"mod", "s3m", "xm", "it", "mo3", "umx"}) {
        auto path = (std::filesystem::path(argv[1]) / "test" / (std::string("test.") + ext)).string();
        auto bytes = read_audio_file(path.c_str());
        check(module->open_memory(path, bytes, &rate, false) && module->is_module(), "OpenMPT fixture from memory");
        auto pcm = render(*module, 4410); double energy = 0;
        for (float v : pcm) { check(std::isfinite(v), "finite module PCM"); energy += std::abs(v); }
        check(energy > 0, "audible module fixture");
        check(module->seek(1000), "OpenMPT seek");
    }
    check(module->open_memory("multi.mod", multi_mod(), &rate, false) && module->track_count() == 2, "OpenMPT subsongs");
    check(module->select_track(1) && module->current_track() == 1 && !render(*module, 512).empty(), "OpenMPT track switch");
    check(!module->select_track(2), "OpenMPT invalid track");
    check(std::string(module->song_title()) == "Two subsongs", "OpenMPT native title");
    auto members = std::make_shared<std::vector<archive_member>>();
    members->push_back({"multi.mod", multi_mod()});
    check(music_subsongs("multi.mod", members, 0).size() == 2, "module archive subsong enumeration");
    music_play_archive_async(members, 0, 1); music_wait();
    check(music_isplaying() && music_currenttrack() == 1, "module archive track playback");
    music_stop();
    module->stop(); check(!module->track_count() && !module->is_playing(), "OpenMPT stop clears state");
    std::unique_ptr<auddecode> bad(create_atari());
    for (unsigned n = 0; n < 80; ++n) {
        auto bytes = ym(); bytes.resize(n);
        check(!bad->open_memory("bad.ym", bytes, &rate, false), "truncated YM rejection");
    }
    auto bytes = ym(); be(bytes, 26, 0, 2);
    check(!bad->open_memory("bad.ym", bytes, &rate, false), "zero YM tick rate rejection");
    bytes = sndh(); be(bytes, 2, 65535, 2);
    check(!bad->open_memory("bad.sndh", bytes, &rate, false), "invalid SNDH header rejection");
    bytes.assign(32, 0); std::memcpy(bytes.data(), "ICE!", 4); be(bytes, 4, 32, 4); be(bytes, 8, 100, 4);
    check(!bad->open_memory("bad.sndh", bytes, &rate, false), "invalid ICE bitstream rejection");
    check(!module->open_memory("bad.mod", {1, 2, 3}, &rate, false), "malformed OpenMPT input rejection");
    std::puts("AtariAudio / OpenMPT checks passed");
}
