#include "audiodecode.h"
#include "visualization.h"
#include "decoder_base.h"
#include <libopenmpt/libopenmpt.hpp>
#include <sstream>
#include "libretro.h"
#include "dr_wav.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <vector>

static void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}
static size_t delivered = 0, limit = SIZE_MAX;
static std::vector<int16_t> accepted;
static size_t batch(const int16_t *samples, size_t frames)
{
    frames = std::min(frames, limit);
    delivered += frames;
    accepted.assign(samples, samples + frames * 2);
    return frames;
}
retro_audio_sample_batch_t audio_batch_cb = batch;
retro_audio_sample_t audio_cb = nullptr;

static void put(std::ofstream &file, uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        file.put(char(value >> (8 * i)));
}
static void wave(const std::filesystem::path &path, unsigned rate, unsigned channels, unsigned bits)
{
    std::ofstream f(path, std::ios::binary);
    const unsigned frames = rate * 2 + 17, bytes = frames * channels * bits / 8;
    f.write("RIFF", 4);
    put(f, bytes + 36, 4);
    f.write("WAVEfmt ", 8);
    put(f, 16, 4);
    put(f, 1, 2);
    put(f, channels, 2);
    put(f, rate, 4);
    put(f, rate * channels * bits / 8, 4);
    put(f, channels * bits / 8, 2);
    put(f, bits, 2);
    f.write("data", 4);
    put(f, bytes, 4);
    for (unsigned i = 0; i < frames; ++i)
        for (unsigned c = 0; c < channels; ++c)
        {
            const int32_t sample = int32_t(std::sin(i * 0.07) * (bits == 8 ? 64 : bits == 16 ? 16384
                                                                                             : 4194304));
            put(f, bits == 8 ? sample + 128 : sample, bits / 8);
        }
}
static void check_visualizer()
{
    audio_visualizer visualizer;
    check(visualizer.snapshot().spectrum == visualization_data{}.spectrum, "initial spectrum is silent");
    std::vector<int16_t> samples(4096 * 2);
    for (float frequency : {100.0f, 1000.0f, 10000.0f})
    {
        visualizer.reset();
        for (size_t i = 0; i < samples.size() / 2; ++i)
        {
            samples[2 * i] = int16_t(16384 * std::sin(2 * std::numbers::pi * frequency * i / 44100));
            samples[2 * i + 1] = samples[2 * i];
        }
        visualizer.push(samples.data(), samples.size() / 2);
        const auto normal = visualizer.snapshot();
        const auto peak = std::max_element(normal.spectrum.begin(), normal.spectrum.end());
        const int expected = int(std::log(frequency * 2048 / 44100) / std::log(20000.0 * 2048 / 44100) * 32);
        check(std::abs(int(peak - normal.spectrum.begin()) - expected) <= 1 && *peak > 0.8f,
              "spectrum locates low, mid and high frequency tones");
        for (size_t i = 0; i < normal.left.size(); ++i)
            check(normal.left[i] == samples[(4096 - normal.left.size() + i) * 2] / 32768.0f,
                  "oscilloscope retains the latest frames after ring wrap");
        for (size_t i = 1; i < samples.size(); i += 2)
            samples[i] = -samples[i];
        visualizer.push(samples.data(), samples.size() / 2);
        const auto inverted = visualizer.snapshot();
        check(inverted.spectrum == normal.spectrum, "opposite stereo phases do not cancel the spectrum");
        check(inverted.left.back() == -inverted.right.back(), "oscilloscope keeps stereo channels separate");
    }
    std::fill(samples.begin(), samples.end(), 0);
    visualizer.push(samples.data(), samples.size() / 2);
    check(visualizer.snapshot().spectrum == visualization_data{}.spectrum, "silence clears spectrum without NaNs");
    visualizer.reset();
    check(visualizer.snapshot().left == visualization_data{}.left, "reset clears waveform history");
}

static void check_module_loop(const std::filesystem::path &path)
{
    const auto filename = path.string();
    float rate = 0;
    std::unique_ptr<auddecode> decoder(make_decoder(filename.c_str(), &rate));
    check(decoder && decoder->is_module(), "module detected");
    const unsigned duration = decoder->song_duration();
    check(duration > 10, "module has a first-pass duration");
    const unsigned position = duration - 10;
    decoder->set_loop(true);
    check(decoder->seek(position), "seek to loop boundary");
    const auto bytes = read_audio_file(filename.c_str());
    std::ostringstream log;
    openmpt::module reference(bytes, log, {{"seek.sync_samples", "1"}});
    reference.select_subsong(0);
    reference.set_repeat_count(-1);
    reference.set_position_seconds(double(position) / 1000);
    for (int i = 0; i < 8; ++i)
    {
        unsigned count = 997;
        float *audio = nullptr;
        decoder->mix(audio, count);
        std::vector<float> expected(997 * 2);
        check(count == 997 && reference.read_interleaved_stereo(int(rate), 997, expected.data()) == 997,
              "loop produces full blocks beyond duration");
        check(std::equal(expected.begin(), expected.end(), audio), "native OpenMPT loop preserves tracker state without rewinding");
    }
    decoder->set_loop(false);
    unsigned count = 512;
    float *audio = nullptr;
    decoder->mix(audio, count);
    check(!count && !decoder->is_playing(), "disabling loop beyond duration stops decoder");
    decoder.reset();

    music_repeat(true);
    check(music_play(filename.c_str()) && music_islooping(), "repeat before load enables module looping");
    music_setposition(position);
    for (int i = 0; i < 10; ++i)
        music_run();
    check(music_isplaying() && music_getposition() > duration, "player continues and reports elapsed time beyond first pass");
    music_pause(true);
    const auto paused_position = music_getposition();
    music_run();
    check(music_getposition() == paused_position, "pause holds looping position");
    music_pause(false);
    music_repeat(false);
    for (int i = 0; i < 4; ++i)
        music_run();
    check(!music_isplaying(), "disabling repeat drains and stops module");
    music_repeat(true);
    music_run();
    check(music_isplaying(), "enabling repeat after EOF restarts module");
    music_setposition(0);
    check(music_getposition() == 0, "looping module can restart");
    music_stop();
    music_repeat(false);
}

int main(int argc, char **argv)
{
    check(argc == 2, "source directory argument");
    const std::filesystem::path source(argv[1]);
    {
        const auto original = source / "test/test.wv";
        const std::filesystem::path copy = "generated-buffered.wv";
        std::filesystem::copy_file(original, copy, std::filesystem::copy_options::overwrite_existing);
        float rate = 0;
        std::unique_ptr<auddecode> decoder(make_decoder(copy.string().c_str(), &rate));
        check(bool(decoder), "open buffered WavPack");
        check(std::filesystem::remove(copy), "WavPack closes file before playback");
        check(decoder->seek(1000), "buffered WavPack seeks after source disappears");
        float *samples = nullptr;
        unsigned frames = 512;
        decoder->mix(samples, frames);
        check(frames == 512 && samples, "buffered WavPack decodes without filesystem access");
    }
    check_visualizer();
    for (const char *type : {"mod", "s3m", "xm", "it"})
        check_module_loop(source / "test" / (std::string("test.") + type));
    const char *types[] = {"wav", "flac", "mp3", "ogg", "opus", "m4a", "mpc", "wv", "mod", "s3m", "xm", "it"};
    for (const char *type : types)
    {
        const auto path = (source / "test" / (std::string("test.") + type)).string();
        float rate = 0;
        std::unique_ptr<auddecode> d(make_decoder(path.c_str(), &rate));
        std::printf("%s: rate %.0f, duration %u\n", type, rate, d ? d->song_duration() : 0);
        std::fflush(stdout);
        check(bool(d), "fixture opens");
        double energy = 0;
        for (unsigned i = 0; i < 160; ++i)
        {
            unsigned frames = i % 2 ? 997 : 113;
            float *samples = nullptr;
            const auto requested = frames;
            d->mix(samples, frames);
            check(frames <= requested && (!frames || samples), "frame and buffer contract");
            for (unsigned j = 0; j < frames * 2; ++j)
            {
                check(std::isfinite(samples[j]), "finite decoded samples");
                energy += std::abs(samples[j]);
            }
        }
        check(energy > 1, "audible decoded output");
        check(d->seek(0), "rewind");
        check(d->seek(1000), "seek forward");
        if (d->song_duration() > 2000)
        {
            check(d->seek(d->song_duration() - 1000), "seek near EOF");
            unsigned iterations = 0;
            while (d->is_playing() && ++iterations < 10000)
            {
                float *samples = nullptr;
                unsigned count = 1024;
                d->mix(samples, count);
            }
            check(!d->is_playing(), "EOF terminates playback");
            check(d->seek(0) && d->is_playing(), "seek after EOF restarts");
        }
        d->stop();
        d->stop();
    }
    {
        drwav output = {};
        drwav_data_format format = {drwav_container_w64, DR_WAVE_FORMAT_PCM, 2, 44100, 16};
        check(drwav_init_file_write(&output, "generated.w64", &format, nullptr), "create Wave64 fixture");
        std::vector<int16_t> samples(44100 * 2 * 2, 1024);
        check(drwav_write_pcm_frames(&output, 88200, samples.data()) == 88200, "write Wave64 frames");
        drwav_uninit(&output);
        check(music_play("generated.w64") && music_getduration() == 2000, "Wave64 remains supported");
        for (unsigned i = 0; i < 130; ++i)
            music_run();
        check(!music_isplaying() && music_getposition() == 2000, "Wave64 reaches EOF");
        music_stop();
        std::filesystem::remove("generated.w64");
    }
    for (unsigned rate : {22050u, 44100u, 48000u, 96000u})
    {
        for (unsigned bits : {8u, 16u, 24u})
        {
            const auto path = std::filesystem::absolute("generated-mono.WAV");
            wave(path, rate, 1, bits);
            float actual_rate = 0;
            std::unique_ptr<auddecode> d(make_decoder(path.string().c_str(), &actual_rate));
            check(d && actual_rate == rate, "uppercase WAV and source rate");
            uint64_t total = 0;
            while (d->is_playing())
            {
                unsigned count = 127;
                float *samples = nullptr;
                d->mix(samples, count);
                total += count;
                for (unsigned i = 0; i < count; ++i)
                    check(samples[i * 2] == samples[i * 2 + 1], "mono duplicates both channels");
            }
            check(total == rate * 2 + 17, "partial final read preserves every source frame");
            d.reset();
            check(music_play(path.string().c_str()), "player opens generated WAV");
            delivered = 0;
            for (int i = 0; i < 60; ++i)
                music_run();
            check(delivered == 44100 && music_getposition() == 1000, "one second at all source rates");
            music_pause(true);
            music_run();
            check(music_getposition() == 1000, "pause holds position");
            for (int i = 0; i < 3; ++i)
                music_run();
            check(music_visualization().spectrum == visualization_data{}.spectrum, "paused output settles to silence");
            music_setposition(250);
            check(music_getposition() == 250, "seek while paused");
            check(music_visualization().left == visualization_data{}.left, "seek clears visualization");
            music_pause(false);
            music_run();
            check(music_getposition() == 266, "seek resets output history");
            limit = 100;
            const auto before = music_getposition();
            music_run();
            check(music_getposition() <= before + 3, "short frontend writes preserve queued audio");
            limit = SIZE_MAX;
            check(accepted.size() == 200, "frontend accepted short batch");
            const auto &visual = music_visualization();
            for (size_t i = 0; i < 100; ++i)
                check(visual.left[visual.left.size() - 100 + i] == accepted[2 * i] / 32768.0f,
                      "visualization captures only accepted frontend frames");
            for (int i = 0; i < 150; ++i)
                music_run();
            check(!music_isplaying(), "player reaches EOF");
            music_repeat(true);
            music_run();
            check(music_isplaying(), "repeat restarts at EOF");
            music_repeat(false);
            music_stop();
            music_stop();
            check(music_getduration() == 0 && music_getposition() == 0, "stop clears state");
            check(music_visualization().left == visualization_data{}.left, "stop clears visualization");
            std::filesystem::remove(path);
        }
    }
    for (const char *type : types)
    {
        const auto path = std::filesystem::absolute(std::string("invalid.") + type);
        {
            std::ofstream f(path, std::ios::binary);
            f << "not an audio file";
        }
        check(!music_play(path.string().c_str()), "malformed input fails safely");
        std::filesystem::remove(path);
    }
    check(!music_play("missing.wav"), "missing file fails safely");
    check(!music_play(nullptr), "null filename fails safely");
    check(!auddecode_supports("not.a") && auddecode_supports("valid.MP3") && auddecode_supports("valid.IT"), "exact case-insensitive extensions");
    std::puts("Decoder and playback checks passed.");
}
