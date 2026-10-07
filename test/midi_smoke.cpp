#include "audiodecode.h"
#include "midi_resources.h"
#include "replayer_settings.h"
#include "libretro.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>

retro_audio_sample_batch_t audio_batch_cb = [](const int16_t *, size_t n) { return n; };
retro_audio_sample_t audio_cb = nullptr;
using bytes = std::vector<uint8_t>;
static void check(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void le(bytes &b, uint32_t value, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) b.push_back(uint8_t(value >> (8 * i)));
}
static void name(bytes &b, const char *s, size_t size)
{
    const auto start = b.size();
    while (*s && b.size() < start + size) b.push_back(uint8_t(*s++));
    b.resize(start + size);
}
static bytes chunk(const char *id, bytes payload)
{
    bytes b; name(b, id, 4); le(b, uint32_t(payload.size()), 4);
    b.insert(b.end(), payload.begin(), payload.end());
    if (payload.size() & 1) b.push_back(0);
    return b;
}
static void append(bytes &out, const bytes &b) { out.insert(out.end(), b.begin(), b.end()); }
static bytes list(const char *type, const bytes &b)
{
    bytes data; name(data, type, 4); append(data, b); return chunk("LIST", data);
}
// Original sine sample in a minimal standards-compliant SF2 bank. No third-party assets.
static bytes soundfont()
{
    bytes info, version; le(version, 2, 2); le(version, 1, 2);
    append(info, chunk("ifil", version));
    bytes label; name(label, "Detonate MIDI test", 20); append(info, chunk("INAM", label));
    bytes sample;
    for (unsigned i = 0; i < 1024; ++i)
        le(sample, uint16_t(int16_t(std::sin(i * 6.283185307179586 / 64) * 12000)), 2);
    sample.resize(sample.size() + 46 * 2);
    bytes pdta, phdr;
    for (unsigned i = 0; i < 2; ++i)
    {
        name(phdr, i ? "EOP" : "Sine", 20);
        le(phdr, 0, 2); le(phdr, 0, 2); le(phdr, i, 2);
        le(phdr, 0, 4); le(phdr, 0, 4); le(phdr, 0, 4);
    }
    append(pdta, chunk("phdr", phdr));
    append(pdta, chunk("pbag", {0,0,0,0,1,0,0,0}));
    append(pdta, chunk("pmod", bytes(10)));
    append(pdta, chunk("pgen", {41,0,0,0,0,0,0,0})); // instrument 0, terminal
    bytes inst; name(inst, "Sine", 20); le(inst, 0, 2); name(inst, "EOI", 20); le(inst, 1, 2);
    append(pdta, chunk("inst", inst));
    append(pdta, chunk("ibag", {0,0,0,0,2,0,0,0}));
    append(pdta, chunk("imod", bytes(10)));
    append(pdta, chunk("igen", {54,0,1,0,53,0,0,0,0,0,0,0})); // looping sample 0
    bytes shdr;
    for (unsigned i = 0; i < 2; ++i)
    {
        name(shdr, i ? "EOS" : "Sine", 20);
        le(shdr, i ? 1024 : 0, 4); le(shdr, 1024, 4);
        le(shdr, i ? 1024 : 0, 4); le(shdr, 1024, 4);
        le(shdr, 44100, 4); shdr.push_back(60); shdr.push_back(0);
        le(shdr, 0, 2); le(shdr, 1, 2);
    }
    append(pdta, chunk("shdr", shdr));
    bytes sf; name(sf, "sfbk", 4);
    append(sf, list("INFO", info)); append(sf, list("sdta", chunk("smpl", sample)));
    append(sf, list("pdta", pdta)); return chunk("RIFF", sf);
}
static void be(bytes &b, uint32_t value, unsigned count)
{
    for (unsigned i = count; i; --i) b.push_back(uint8_t(value >> (8 * (i - 1))));
}
static void track(bytes &out, const bytes &data)
{
    name(out, "MTrk", 4); be(out, uint32_t(data.size()), 4); append(out, data);
}
static bytes song(unsigned format)
{
    bytes b; name(b, "MThd", 4); be(b, 6, 4); be(b, format, 2); be(b, format ? 2 : 1, 2); be(b, 480, 2);
    const bytes tempo{0,0xff,0x51,3,7,0xa1,0x20,0x83,0x60,0xff,0x51,3,3,0xd0,0x90,0,0xff,0x2f,0};
    bytes notes{0,0xff,3,4,'T','e','s','t',0,0xc0,0,0,0x90,60,100,
                0x87,0x40,0x80,60,0,0,0xff,0x2f,0};
    if (format) { track(b, tempo); track(b, notes); }
    else
    {
        // Tempo change halfway through the held note, at 480 ticks.
        notes = {0,0xff,3,4,'T','e','s','t',0,0xc0,0,0,0x90,60,100,
                 0x83,0x60,0xff,0x51,3,3,0xd0,0x90,0x83,0x60,0x80,60,0,0,0xff,0x2f,0};
        track(b, notes);
    }
    return b;
}
static std::vector<float> mix(auddecode &d, unsigned count)
{
    float *pcm = nullptr; unsigned n = count; d.mix(pcm, n);
    check(n == count && pcm, "complete MIDI audio block");
    check(std::all_of(pcm, pcm + n * 2, [](float f) { return std::isfinite(f); }), "finite MIDI audio");
    return {pcm, pcm + n * 2};
}
static void write(const std::filesystem::path &p, const bytes &b)
{
    std::ofstream out(p, std::ios::binary); out.write(reinterpret_cast<const char *>(b.data()), b.size());
    check(bool(out), "write MIDI fixture");
}
int main()
{
    const auto temp = std::filesystem::temp_directory_path() / ("detonate-midi-test-" + std::to_string(std::random_device{}()));
    check(std::filesystem::create_directory(temp), "private test directory");
    struct cleanup { std::filesystem::path p; ~cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); } } clean{temp};
    std::filesystem::create_directories(temp / "system" / "detonatemidi");
    midi_set_system_directory(temp / "system");
    const auto resources = midi_resource_directory();
    check(resources == temp / "system" / "detonatemidi", "system ROM subfolder");
    replayer_settings::defaults();
    for (const char *ext : {"mid", "midi", "rmi", "kar"})
        check(auddecode_supports((std::string("song.") + ext).c_str()), "MIDI extension discovery");
    for (int backend = 0; backend <= 4; ++backend)
    {
        replayer_settings::set(replayer_settings::midi_backend, backend);
        check(!music_play_memory("song.mid", song(0)), "missing MIDI resources fail");
        check(music_error().find("detonatemidi") != std::string::npos, "actionable MIDI resource error");
    }
    replayer_settings::set(replayer_settings::midi_backend, 0);
    write(resources / "detonate.sf2", soundfont());
    for (unsigned format = 0; format <= 1; ++format)
    {
        std::unique_ptr<auddecode> d(create_midi()); float rate = 0;
        auto bytes = song(format);
        check(d->open_memory("song.mid", bytes, &rate, false), "SMF memory playback");
        check(rate == 44100 && d->supports_looping(), "MIDI output and repeat support");
        check(std::string(d->song_title()) == "Test", "MIDI title");
        check(d->song_duration() >= 3749 && d->song_duration() <= 3751, "tempo map and release tail duration");
        const auto reference = mix(*d, 22050);
        check(std::any_of(reference.begin(), reference.end(), [](float v) { return std::abs(v) > .001f; }), "audible synthesized note");
        check(d->seek(250), "MIDI seek restores synth state");
        auto sought = mix(*d, 4096);
        check(std::equal(sought.begin(), sought.end(), reference.begin() + 11025 * 2,
                         [](float a, float b) { return std::abs(a - b) < 1e-5f; }), "seek audio matches continuous playback");
        check(d->seek(0), "MIDI rewind");
        const auto duration = d->song_duration();
        float *pcm = nullptr; unsigned frames = unsigned(uint64_t(duration + 100) * 44100 / 1000);
        d->mix(pcm, frames);
        check(frames >= 165370 && frames <= 165376 && !d->is_playing(), "MIDI EOF");
        check(d->seek(0), "rewind after EOF"); d->set_loop(true);
        auto repeated = mix(*d, 44100 * 2);
        check(std::any_of(repeated.begin() + 44100 * 2, repeated.end(), [](float v) { return std::abs(v) > .001f; }), "repeated MIDI produces audio");
        d->set_loop(false); d->stop(); check(!d->is_playing(), "MIDI stop");
        write(temp / "song.mid", bytes);
        check(d->open((temp / "song.mid").string().c_str(), &rate, false), "MIDI disk playback");
    }
    bytes rmid; name(rmid, "RMID", 4); append(rmid, chunk("data", song(1)));
    check(music_play_memory("song.rmi", chunk("RIFF", rmid)), "RIFF MIDI archive playback");
    music_stop();
    std::unique_ptr<auddecode> invalid(create_midi()); float rate;
    check(!invalid->open_memory("bad.mid", {1,2,3,4}, &rate, false), "invalid MIDI rejected");
    check(!invalid->open_memory("bad.mid", {}, &rate, false), "empty MIDI rejected");
    auto truncated = song(0); truncated.pop_back();
    check(!invalid->open_memory("bad.mid", truncated, &rate, false), "truncated track rejected");
    auto oversized = song(0); oversized[18] = 0x7f;
    check(!invalid->open_memory("bad.mid", oversized, &rate, false), "out-of-bounds chunk rejected");
    auto smpte = song(0); smpte[12] = uint8_t(-25); smpte[13] = 40;
    check(invalid->open_memory("smpte.mid", smpte, &rate, false), "SMPTE MIDI playback");
    check(invalid->song_duration() >= 3959 && invalid->song_duration() <= 3961, "SMPTE ignores tempo changes");
    invalid->stop();
    // A zero-time song must not trap the sequencer in its repeat rewind loop.
    bytes instant; name(instant, "MThd", 4); be(instant, 6, 4); be(instant, 0, 2); be(instant, 1, 2); be(instant, 480, 2);
    track(instant, {0,0xc0,0,0,0xff,0x2f,0});
    check(invalid->open_memory("instant.mid", instant, &rate, true), "zero-time repeated MIDI opens");
    mix(*invalid, 4096);
    std::puts("MIDI smoke passed (SpessaSynth audio; hardware ROM failure paths).");
}
