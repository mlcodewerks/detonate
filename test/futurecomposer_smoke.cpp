#include "audiodecode.h"
#include "audiotags.hpp"
#include "archive_reader.h"
#include "libretro.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

retro_audio_sample_batch_t audio_batch_cb = [](const int16_t *, size_t n) { return n; };
retro_audio_sample_t audio_cb = nullptr;
using bytes = std::vector<uint8_t>;
static void check(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s (%s)\n", message, music_error().c_str()); std::exit(1); }
}
static void be(bytes &b, size_t p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[p + i] = uint8_t(v >> (24 - 8 * i));
}
static void le(bytes &b, size_t p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[p + i] = uint8_t(v >> (8 * i));
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
    b[pattern] = 24;
    b[sound] = 0xe2; b[sound + 1] = 10; b[sound + 3] = 0xe1;
    b[volume] = 1; b[volume + 5] = 64; b[volume + 6] = 0xe1;
    if (!old) {
        be(b, 36, wave); b[100] = 16;
        for (unsigned i = 0; i < 32; ++i) b[wave + i] = i < 16 ? 64 : 192;
    }
    return b;
}
static bytes tagged(bytes b, bool ape) {
    if (!ape) {
        const auto p = b.size(); b.resize(p + 128);
        std::memcpy(b.data() + p, "TAG", 3);
        std::memcpy(b.data() + p + 3, "Song", 4);
        std::memcpy(b.data() + p + 33, "Artist", 6);
        std::memcpy(b.data() + p + 63, "Album", 5);
    } else {
        const auto start = b.size();
        for (const auto &[key, value] : {std::pair{"Title", "Song"}, {"Artist", "Artist"}, {"Album", "Album"}}) {
            const auto p = b.size(); b.resize(p + 8); le(b, p, uint32_t(std::strlen(value)));
            b.insert(b.end(), key, key + std::strlen(key) + 1);
            b.insert(b.end(), value, value + std::strlen(value));
        }
        const auto p = b.size(); b.resize(p + 32);
        std::memcpy(b.data() + p, "APETAGEX", 8); le(b, p + 8, 2000);
        le(b, p + 12, uint32_t(b.size() - start)); le(b, p + 16, 3);
    }
    return b;
}
static std::vector<float> render(auddecode &d, unsigned frames) {
    float *pcm = nullptr; unsigned n = frames; d.mix(pcm, n);
    check(n == frames, "complete PCM block");
    return {pcm, pcm + n * 2};
}
// Two short silent TFMX subsongs, wrapped in the native tagged single-file format.
static bytes native_tags() {
    const unsigned h = 20, samples = h + 0x850, tag = samples + 64;
    bytes b(tag);
    std::memcpy(b.data(), "TFMX-MOD", 8); le(b, 8, samples); le(b, 12, tag);
    std::memcpy(b.data() + h, "TFMX-SONG ", 10);
    for (unsigned i = 0; i < 32; ++i) {
        b[h + 0x100 + i * 2] = 1; b[h + 0x101 + i * 2] = 0xff;
        b[h + 0x140 + i * 2] = 1; b[h + 0x141 + i * 2] = 0xff;
    }
    for (unsigned i = 0; i < 2; ++i) {
        b[h + 0x100 + i * 2] = 0; b[h + 0x101 + i * 2] = uint8_t(i);
        b[h + 0x140 + i * 2] = 0; b[h + 0x141 + i * 2] = uint8_t(i);
        b[h + 0x181 + i * 2] = 3;
    }
    be(b, h + 0x400, 0x820); be(b, h + 0x600, 0x840);
    b[h + 0x820] = 0xf3; b[h + 0x821] = 8; b[h + 0x824] = 0xf0;
    b[h + 0x840] = 7;
    for (const auto &[kind, value] : {std::pair{1, "Native Artist"}, {2, "Native Game"}, {6, "Native Title"}}) {
        b.push_back(uint8_t(kind)); b.push_back(uint8_t(std::strlen(value))); b.push_back(0);
        b.insert(b.end(), value, value + std::strlen(value));
    }
    b.resize(b.size() + 3);
    return b;
}
int main() {
    for (const char *ext : {"fc", "fc13", "fc14", "fc3", "fc4", "smod", "hip", "hip7", "hipc", "mcmd", "tfmx", "tfx", "tfm", "mdat", "dns"})
        check(auddecode_supports((std::string("song.") + ext).c_str()), "extension advertised");
    check(auddecode_supports("FC14.Song") && auddecode_supports("SMOD.Song"), "Amiga filename prefixes");
    for (bool old : {false, true}) {
        auto b = module(old);
        audiotags::Metadata meta;
        check(audiotags::read_memory(b.data(), b.size(), meta), "recognize raw FC metadata");
        check(meta.format == audiotags::Format::future_composer && meta.channels == 4, "FC format and voices");
        check(meta.title.empty() && meta.artist.empty(), "no invented raw FC tags");
        std::unique_ptr<auddecode> d(create_futurecomposer()); float rate = 0;
        check(d->open_memory(old ? "song.fc13" : "song.fc14", b, &rate, false), "open module from memory");
        check(rate == 44100 && d->track_count() == 1 && !d->select_track(1), "rate and track bounds");
        const auto pcm = render(*d, 4410);
        double energy = 0;
        for (float v : pcm) { check(std::isfinite(v), "finite PCM"); energy += std::abs(v); }
        check(energy > 1, "audible synthesis");
        check(d->seek(0) && render(*d, 4410) == pcm, "deterministic rewind");
        const auto next = render(*d, 997);
        check(d->seek(100) && render(*d, 997) == next, "sample-exact forward seek");
        const auto duration = d->song_duration(); check(duration > 100 && duration < 10000, "native duration");
        check(d->seek(duration), "seek to end");
        unsigned n = 1; float *p = nullptr; d->mix(p, n); check(!n && !d->is_playing(), "EOF without repeat");
        d->set_loop(true); render(*d, 4096); check(d->is_playing(), "repeat resumes at native loop");
        check(d->select_track(0), "restart track");
        render(*d, rate * (duration / 1000 + 2));
        for (bool ape : {false, true}) {
            auto t = tagged(b, ape);
            check(audiotags::read_memory(t.data(), t.size(), meta) && meta.title == "Song" && meta.artist == "Artist" && meta.album == "Album", "read appended tags");
            check(d->open_memory("tagged.fc", t, &rate, false), "open tagged FC");
            check(std::string(d->song_title()) == "Song" && std::string(d->song_artist()) == "Artist" && std::string(d->song_album()) == "Album", "decoder exposes tags");
            check(d->seek(100) && std::string(d->song_artist()) == "Artist", "tags survive reset");
        }
        for (size_t length : {size_t(3), size_t(99), b.size() - 1}) {
            bytes truncated(b.begin(), b.begin() + length);
            check(!d->open_memory("bad.fc", truncated, &rate, false), "reject truncated module");
        }
        auto corrupt = b; be(corrupt, 16, 0xfffffff0);
        check(!d->open_memory("bad.fc", corrupt, &rate, false), "reject out-of-range sequence");
        // Exercise actual factory routing and the background archive ownership path.
        auto members = std::make_shared<std::vector<archive_member>>();
        members->push_back({"FC14.Memory", tagged(b, true)});
        music_play_archive_async(members, 0); members.reset(); music_wait(); music_poll();
        check(music_isplaying() && music_title().find("Song") != std::string::npos, "async archive playback and tags");
        music_stop();
        const auto path = std::filesystem::path(u8"futurecomposer-\u97f3\u697d.FC");
        { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char *>(b.data()), b.size()); check(bool(f), "write Unicode fixture"); }
        const auto u8 = path.u8string(); const std::string name(u8.begin(), u8.end());
        music_play_async(name); music_wait(); music_poll();
        check(music_isplaying() && music_title().find("futurecomposer-") != std::string::npos, "Unicode file playback and filename title");
        music_stop(); std::filesystem::remove(path);
    }
    std::unique_ptr<auddecode> native(create_futurecomposer()); float rate = 0;
    check(native->open_memory("tagged.tfmx", native_tags(), &rate, false), "open native tagged TFMX");
    check(native->track_count() == 2, "TFMX subsongs");
    auto tagged_members = std::make_shared<const std::vector<archive_member>>(
        std::vector<archive_member>{{"tagged.tfmx", native_tags()}});
    const auto subsongs = music_subsongs("tagged.tfmx", tagged_members, 0);
    check(subsongs.size() == 2, "module subsongs exposed to browser");
    for (const auto &song : subsongs)
        check(song.title == "Native Title" && song.artist == "Native Artist" && song.album == "Native Game" &&
            song.duration > 0, "module subsong tags and duration");
    for (unsigned i = 0; i < 2; ++i) {
        check(native->select_track(i) && native->seek(100), "select and seek native subsong");
        check(std::string(native->song_title()) == "Native Title" && std::string(native->song_artist()) == "Native Artist" && std::string(native->song_album()) == "Native Game", "native title/artist/game survive subsong and seek");
    }
    check(native->open_memory("override.tfmx", tagged(native_tags(), true), &rate, false) && std::string(native->song_title()) == "Song", "APEv2 overrides native metadata");
    const auto merged = native_tags();
    bytes mdat(merged.begin() + 20, merged.begin() + 20 + 0x850);
    check(!native->open_memory("mdat.fixture", mdat, &rate, false), "memory-only decoder requires missing companion");
    auto pair = std::make_shared<std::vector<archive_member>>();
    pair->push_back({"music/mdat.fixture", mdat});
    pair->push_back({"music/smpl.fixture", bytes(64)});
    check(music_subsongs("music/mdat.fixture", pair, 0).size() == 2, "probe module subsongs with archive companions");
    music_play_archive_async(pair, 0); pair.reset(); music_wait(); music_poll();
    check(music_isplaying() && music_trackcount() == 2, "archive sample companion fallback");
    music_settrack_async(1); music_wait(); music_poll();
    check(music_isplaying() && music_currenttrack() == 1, "background companion subsong selection");
    music_stop();
    std::puts("FutureComposer playback, memory, async, tags, subsongs, seek, repeat and malformed-input checks passed");
}
