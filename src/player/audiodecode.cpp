#include "audiodecode.h"
#include "visualization.h"
#include "archive_reader.h"
#include "libretro.h"
#include <audio/audio_resampler.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <set>
#include <future>
#include <functional>
#include <chrono>

extern retro_audio_sample_batch_t audio_batch_cb;
extern retro_audio_sample_t audio_cb;
extern "C" retro_resampler_t sinc_resampler;
namespace
{
    constexpr unsigned output_rate = 44100, tick_frames = output_rate / 60;
    using factory = auddecode *(*)();
    constexpr factory factories[] = {create_common, create_wav, create_mpc, create_wv, create_vgm, create_gme, create_sid, create_v2m, create_organya, create_klystrack, create_futurecomposer, create_ken, create_tfmx, create_hively, create_psx, create_sega, create_qsf, create_usf, create_gsf, create_snsf, create_2sf};
    struct temporary_audio
    {
        std::filesystem::path directory, audio;
        ~temporary_audio()
        {
            std::error_code ec;
            if (!directory.empty())
                std::filesystem::remove_all(directory, ec);
        }
    };
    std::string extension(const char *filename)
    {
        if (!filename)
            return {};
        auto base = std::filesystem::path(reinterpret_cast<const char8_t *>(filename)).filename().u8string();
        std::string lowerbase(base.begin(), base.end());
        std::transform(lowerbase.begin(), lowerbase.end(), lowerbase.begin(), [](unsigned char c)
                       { return std::tolower(c); });
        for (const auto *prefix : {"mdat", "fc", "fc13", "fc14", "fc3", "fc4", "smod", "hip", "hip7", "hipc", "mcmd", "dns"})
            if (lowerbase.starts_with(std::string(prefix) + "."))
                return prefix;

        std::string ext = std::filesystem::path(reinterpret_cast<const char8_t *>(filename)).extension().string();
        if (!ext.empty())
            ext.erase(0, 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                       { return std::tolower(c); });
        return ext;
    }
    const std::set<std::string> &extensions()
    {
        static const auto result = []
        {
            std::set<std::string> types;
            for (auto create : factories)
            {
                std::unique_ptr<auddecode> candidate(create());
                for (const auto &ext : candidate->file_types())
                    types.insert(ext);
            }
            return types;
        }();
        return result;
    }
    struct music_state
    {
        void *resampler = nullptr;
        float source_rate = 0;
        bool paused = false, repeat = false, flushed = false;
        uint64_t played = 0, source_frames = 0, produced_frames = 0;
        uint32_t seek_base = 0;
        std::vector<float> resampled;
        std::vector<int16_t> queue;
        size_t queue_offset = 0;
        std::string error;
        std::string source_name;

        std::unique_ptr<temporary_audio> archived_audio;
        std::unique_ptr<auddecode> decoder;
        audio_visualizer visualizer;
        size_t send(const int16_t *samples, size_t frames)
        {
            if (audio_batch_cb)
                frames = std::min(frames, audio_batch_cb(samples, frames));
            else if (audio_cb)
                for (size_t i = 0; i < frames; ++i)
                    audio_cb(samples[2 * i], samples[2 * i + 1]);
            visualizer.push(samples, frames);
            return frames;
        }
        void append(const float *samples, size_t frames)
        {
            for (size_t i = 0; i < frames * 2; ++i)
            {
                float value = std::isfinite(samples[i]) ? std::clamp(samples[i], -1.0f, 1.0f) : 0.0f;
                queue.push_back(static_cast<int16_t>(std::clamp(value * 32768.0f, -32768.0f, 32767.0f)));
            }
            produced_frames += frames;
        }
        void clear_pipeline()
        {
            visualizer.reset();
            queue.clear();
            queue_offset = 0;
            played = source_frames = produced_frames = 0;
            flushed = false;
            if (resampler)
                sinc_resampler.reset(resampler);
        }
        const std::string &music_error();
        bool music_isplaying();
        void music_pause(bool);
        void music_repeat(bool);
        bool music_islooping();
        unsigned music_trackcount();
        unsigned music_currenttrack();
        std::string music_tracktitle(unsigned);
        std::string music_title();
        bool music_settrack(unsigned);
        const visualization_data &music_visualization();
        void music_stop();
        bool finish_open(const char *);
        bool music_play(const char *);
        bool music_play_memory(const std::string &, const std::vector<uint8_t> &,
                               const std::vector<uint8_t> *, const std::vector<archive_member> *);
        uint32_t music_getduration();
        uint32_t music_getposition();
        void music_setposition(uint64_t);
        void music_run();
        ~music_state() { music_stop(); }
    };
}

std::string auddecode_formats()
{
    std::string result;
    for (const auto &ext : extensions())
    {
        if (!result.empty())
            result += '|';
        result += ext;
    }
    return result;
}
bool auddecode_supports(const char *filename) { return extensions().count(extension(filename)) != 0; }
auddecode *make_decoder(const char *filename, float *rate)
{
    if (!filename || !rate)
        return nullptr;
    const auto ext = extension(filename);
    for (auto create : factories)
    {
        std::unique_ptr<auddecode> candidate(create());
        const auto types = candidate->file_types();
        if (std::find(types.begin(), types.end(), ext) != types.end() && candidate->open(filename, rate, false))
            return candidate.release();
    }
    return nullptr;
}
const std::string &music_state::music_error() { return error; }
bool music_state::music_isplaying() { return decoder && (decoder->is_playing() || queue_offset < queue.size()); }
void music_state::music_pause(bool value) { paused = value; }
void music_state::music_repeat(bool value)
{
    repeat = value;
    if (decoder && decoder->supports_looping())
    {
        decoder->set_loop(value);
        if (value)
            flushed = false;
    }
    else if (value && decoder && !music_isplaying() && decoder->seek(0))
    {
        clear_pipeline();
        seek_base = 0;
    }
}
bool music_state::music_islooping() { return decoder && decoder->supports_looping() && repeat; }
unsigned music_state::music_trackcount() { return decoder ? decoder->track_count() : 0; }
unsigned music_state::music_currenttrack() { return decoder ? decoder->current_track() : 0; }
std::string music_state::music_tracktitle(unsigned index) { return decoder ? decoder->track_title(index) : ""; }
std::string music_state::music_title()
{
    if (!decoder)
        return {};
    const char *title = decoder->song_title();
    const char *artist = decoder->song_artist();
    const char *album = decoder->song_album();
    std::string result;
    if (artist && *artist)
        result = std::string(artist) + " ";
    if (album && *album)
        result += "[" + std::string(album) + "] ";
    if (title && *title)
        return result + title;
    const auto name = std::filesystem::path(reinterpret_cast<const char8_t *>(source_name.c_str())).stem().u8string();
    return result + std::string(name.begin(), name.end());
}
bool music_state::music_settrack(unsigned index)
{
    if (!decoder || index >= decoder->track_count())
    {
        error = "Unable to play this track.";
        return false;
    }
    try
    {
        if (decoder->select_track(index))
        {
            clear_pipeline();
            seek_base = 0;
            error.clear();
            return true;
        }
        error = "Unable to play this track.";
    }
    catch (const std::exception &e)
    {
        error = e.what();
    }
    return false;
}
const visualization_data &music_state::music_visualization() { return visualizer.snapshot(); }
void music_state::music_stop()
{
    source_name.clear();
    decoder.reset();
    archived_audio.reset();
    if (resampler)
        sinc_resampler.free(resampler);
    resampler = nullptr;
    source_rate = 0;
    paused = false;
    seek_base = 0;
    clear_pipeline();
}
bool music_state::finish_open(const char *filename)
{
    if (!decoder || source_rate < 8000 || source_rate > 384000)
    {
        music_stop();
        error = "Unable to open this audio file.";
        return false;
    }
    if (decoder->supports_looping())
        decoder->set_loop(repeat);
    source_name = filename;
    if (source_rate != output_rate)
    {
        resampler = sinc_resampler.init(nullptr, output_rate / double(source_rate), RESAMPLER_QUALITY_NORMAL, 0);
        if (!resampler)
        {
            music_stop();
            error = "Unable to initialize the audio resampler.";
            return false;
        }
    }
    return true;
}
bool music_state::music_play(const char *filename)
{
    music_stop();
    error.clear();
    try
    {
        decoder.reset(make_decoder(filename, &source_rate));
        return finish_open(filename);
    }
    catch (const std::exception &e)
    {
        music_stop();
        error = e.what();
        return false;
    }
}
bool music_state::music_play_memory(const std::string &name, const std::vector<uint8_t> &bytes,
                                    const std::vector<uint8_t> *rom, const std::vector<archive_member> *companions)
{
    error.clear();
    try
    {
        if (bytes.size() > 256 * 1024 * 1024)
            throw std::runtime_error("Archived music exceeds the size limit.");
        if (!auddecode_supports(name.c_str()))
            throw std::runtime_error("Unsupported archived music file.");
        music_stop();
        const auto ext = extension(name.c_str());
        for (auto create : factories)
        {
            std::unique_ptr<auddecode> candidate(create());
            const auto types = candidate->file_types();
            if (std::find(types.begin(), types.end(), ext) != types.end() &&
                candidate->open_memory(name, bytes, &source_rate, false))
            {
                decoder = std::move(candidate);
                return finish_open(name.c_str());
            }
        }
        auto storage = std::make_unique<temporary_audio>();
        const auto root = std::filesystem::temp_directory_path();
        std::random_device random;
        for (unsigned attempt = 0; attempt < 32; ++attempt)
        {
            const auto path = root / ("detonate-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(path))
            {
                storage->directory = path;
                break;
            }
        }
        if (storage->directory.empty())
            throw std::runtime_error("Unable to create temporary audio directory.");
        auto relative = [](std::string name)
        {
            std::replace(name.begin(), name.end(), '\\', '/');
            auto path = std::filesystem::path(reinterpret_cast<const char8_t *>(name.c_str()));
            if (name.empty() || name.find(':') != std::string::npos || path.is_absolute() || path.has_root_name())
                throw std::runtime_error("Invalid archived companion path.");
            for (const auto &part : path)
                if (part == ".." || part == "." || part.empty())
                    throw std::runtime_error("Invalid archived companion path.");
            return path;
        };
        storage->audio = storage->directory / (companions ? relative(name) : std::filesystem::path("track." + extension(name.c_str())));
        const auto write = [](const std::filesystem::path &path, const std::vector<uint8_t> &data)
        {
            if (data.size() > 256 * 1024 * 1024)
                throw std::runtime_error("Archived music exceeds the size limit.");
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
            file.close();
            if (!file)
                throw std::runtime_error("Unable to write temporary audio file.");
        };
        if (companions)
        {
            if (companions->size() > 16384)
                throw std::runtime_error("Too many archived companion files.");
            size_t total = 0;
            for (const auto &member : *companions)
            {
                if (member.name.empty())
                    continue; // hidden unsafe archive entries
                if (member.bytes.size() > 256 * 1024 * 1024 - total)
                    throw std::runtime_error("Archived companions exceed the size limit.");
                total += member.bytes.size();
                write(storage->directory / relative(member.name), member.bytes);
            }
        }
        write(storage->audio, bytes);
        if (rom)
            write(storage->directory / "yrw801.rom", *rom);
        const auto path = storage->audio.u8string();
        if (!music_play(reinterpret_cast<const char *>(path.c_str())))
            return false;
        archived_audio = std::move(storage);
        source_name = name;
        return true;
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }
}
uint32_t music_state::music_getduration() { return decoder ? decoder->song_duration() : 0; }
uint32_t music_state::music_getposition()
{
    if (!decoder)
        return 0;
    uint64_t ms = seek_base + played * 1000 / output_rate;
    const unsigned duration = music_getduration();
    if (duration && !music_islooping())
        ms = std::min<uint64_t>(ms, duration);
    return static_cast<uint32_t>(std::min<uint64_t>(ms, UINT32_MAX));
}
void music_state::music_setposition(uint64_t ms)
{
    if (!decoder)
        return;
    const unsigned duration = music_getduration();
    ms = std::min<uint64_t>(ms, duration ? duration : UINT32_MAX);
    if (decoder->seek(static_cast<unsigned>(ms)))
    {
        clear_pipeline();
        seek_base = static_cast<uint32_t>(ms);
    }
}
void music_state::music_run()
{
    static const std::array<int16_t, tick_frames * 2> silence = {};
    if (!decoder || paused)
    {
        send(silence.data(), tick_frames);
        return;
    }
    if (queue_offset)
    {
        queue.erase(queue.begin(), queue.begin() + queue_offset);
        queue_offset = 0;
    }
    const double ratio = output_rate / double(source_rate);
    while (queue.size() / 2 < tick_frames && !flushed)
    {
        float *samples = nullptr;
        unsigned count = 512;
        decoder->mix(samples, count);
        if (count)
        {
            source_frames += count;
            if (resampler)
            {
                resampled.resize(static_cast<size_t>(std::ceil((count + 256) * ratio) + 256) * 2);
                resampler_data data = {samples, resampled.data(), count, 0, ratio};
                sinc_resampler.process(resampler, &data);
                append(resampled.data(), data.output_frames);
            }
            else
                append(samples, count);
        }
        else
        {
            if (resampler && source_frames)
            {
                const size_t padding = static_cast<size_t>(std::ceil(256 / std::min(1.0, ratio)));
                std::vector<float> zeros(padding * 2, 0.0f);
                resampled.resize(static_cast<size_t>(std::ceil((padding + 256) * ratio) + 256) * 2);
                resampler_data data = {zeros.data(), resampled.data(), padding, 0, ratio};
                sinc_resampler.process(resampler, &data);
                const uint64_t expected = static_cast<uint64_t>(std::ceil(source_frames * ratio));
                append(resampled.data(), std::min<uint64_t>(data.output_frames, expected > produced_frames ? expected - produced_frames : 0));
            }
            flushed = true;
        }
    }
    size_t frames = std::min<size_t>(tick_frames, queue.size() / 2);
    if (frames)
    {
        const size_t sent = send(queue.data(), frames);
        queue_offset = sent * 2;
        played += sent;
    }
    else if (repeat && source_frames && decoder->seek(0))
    {
        clear_pipeline();
        seek_base = 0;
        music_run();
        return;
    }
    if (frames < tick_frames)
        send(silence.data(), tick_frames - frames);
}

namespace
{
    std::unique_ptr<music_state> current = std::make_unique<music_state>();
    music_state idle;
    std::future<std::unique_ptr<music_state>> playback_job;
    std::function<void(music_state &)> pending_playback;
    bool requested_repeat = false, requested_pause = false;

    void start_playback_job(std::function<void(music_state &)> operation)
    {
        idle.error.clear();
        auto state = std::move(current);
        try
        {
            playback_job = std::async(std::launch::async,
                                      [state = std::move(state), operation = std::move(operation)]() mutable
                                      {
                                          if (!state)
                                              state = std::make_unique<music_state>();
                                          try
                                          {
                                              operation(*state);
                                          }
                                          catch (const std::exception &e)
                                          {
                                              state->music_stop();
                                              state->error = e.what();
                                          }
                                          catch (...)
                                          {
                                              state->music_stop();
                                              state->error = "Unable to load audio.";
                                          }
                                          return std::move(state);
                                      });
        }
        catch (const std::exception &e)
        {
            idle.error = e.what();
        }
    }
    void request_playback(std::function<void(music_state &)> operation)
    {
        if (playback_job.valid())
            pending_playback = std::move(operation);
        else
            start_playback_job(std::move(operation));
    }
    music_state &visible_music() { return current ? *current : idle; }
}
void music_poll()
{
    if (!playback_job.valid() || playback_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    try
    {
        current = playback_job.get();
    }
    catch (const std::exception &e)
    {
        idle.error = e.what();
    }
    if (pending_playback)
    {
        auto next = std::move(pending_playback);
        pending_playback = {};
        start_playback_job(std::move(next));
    }
    else if (current)
    {
        current->music_repeat(requested_repeat);
        current->music_pause(requested_pause);
    }
}
bool music_loading() { return playback_job.valid(); }
void music_wait()
{
    while (playback_job.valid())
    {
        playback_job.wait();
        music_poll();
    }
}
std::vector<subsong_tags> music_subsongs(const std::string &name,
                                         std::shared_ptr<const std::vector<archive_member>> members, size_t member)
{
    const auto ext = extension(name.c_str());
    if (ext == "spc" || ext == "gym" || ext == "rsn")
        return {};
    bool supported = false;
    for (auto create : {create_gme, create_sid, create_tfmx, create_hively, create_futurecomposer})
    {
        std::unique_ptr<auddecode> candidate(create());
        const auto types = candidate->file_types();
        if (std::find(types.begin(), types.end(), ext) != types.end())
        {
            supported = true;
            break;
        }
    }
    if (!supported)
        return {};
    music_state probe;
    if (members)
    {
        const auto &source = members->at(member);
        if (!probe.music_play_memory(source.name, source.bytes, nullptr, members.get()))
            return {};
    }
    else if (!probe.music_play(name.c_str()))
        return {};
    const unsigned count = probe.decoder->track_count();
    if (count <= 1 || count > 4096)
        return {};
    std::vector<subsong_tags> result;
    const auto tag = [](const char *value)
    { return value ? std::string(value) : std::string{}; };
    for (unsigned i = 0; i < count; ++i)
    {
        if (!probe.decoder->select_track(i))
            throw std::runtime_error("Unable to read subsong tags.");
        auto title = tag(probe.decoder->song_title());
        if (title.empty())
            title = probe.decoder->track_title(i);
        result.push_back({std::move(title), tag(probe.decoder->song_artist()),
                          tag(probe.decoder->song_album()), probe.decoder->song_duration()});
    }
    return result;
}

void music_play_async(std::string filename, int track)
{
    requested_pause = false;
    request_playback([filename = std::move(filename), track](music_state &state)
                     {
        if (state.music_play(filename.c_str()) && track >= 0 && !state.music_settrack(unsigned(track)))
            state.music_stop(); });
}
void music_play_archive_async(std::shared_ptr<const std::vector<archive_member>> members, size_t index, int track)
{
    requested_pause = false;
    request_playback([members = std::move(members), index, track](music_state &state)
                     {
        const auto &member = members->at(index);
        const auto slash = member.name.rfind('/');
        const auto rom_name = (slash == std::string::npos ? "" : member.name.substr(0, slash + 1)) + "yrw801.rom";
        const std::vector<uint8_t> *rom = nullptr;
        for (const auto &sibling : *members) if (sibling.name == rom_name) rom = &sibling.bytes;
        if (state.music_play_memory(member.name, member.bytes, rom, members.get()) && track >= 0 &&
            !state.music_settrack(unsigned(track))) state.music_stop(); });
}
void music_stop_async()
{
    requested_pause = false;
    request_playback([](music_state &state)
                     { state.music_stop(); state.error.clear(); });
}
void music_settrack_async(unsigned index)
{
    request_playback([index](music_state &state)
                     { state.music_settrack(index); });
}
void music_setposition_async(uint64_t ms)
{
    request_playback([ms](music_state &state)
                     { state.music_setposition(ms); });
}

bool music_play(const char *filename)
{
    music_wait();
    if (!current)
        current = std::make_unique<music_state>();
    current->repeat = requested_repeat;
    requested_pause = false;
    return current->music_play(filename);
}
bool music_play_memory(const std::string &name, const std::vector<uint8_t> &bytes,
                       const std::vector<uint8_t> *rom, const std::vector<archive_member> *companions)
{
    music_wait();
    if (!current)
        current = std::make_unique<music_state>();
    current->repeat = requested_repeat;
    requested_pause = false;
    return current->music_play_memory(name, bytes, rom, companions);
}
void music_stop()
{
    music_wait();
    if (current)
        current->music_stop();
    requested_pause = false;
}
bool music_settrack(unsigned index)
{
    music_wait();
    return current && current->music_settrack(index);
}
void music_setposition(uint64_t ms)
{
    music_wait();
    if (current)
        current->music_setposition(ms);
}
void music_pause(bool value)
{
    requested_pause = value;
    if (current)
        current->music_pause(value);
}
void music_repeat(bool value)
{
    requested_repeat = value;
    if (current)
        current->music_repeat(value);
}
bool music_ispaused() { return requested_pause; }
bool music_getrepeat() { return requested_repeat; }
bool music_isplaying() { return visible_music().music_isplaying(); }
bool music_islooping() { return visible_music().music_islooping(); }
unsigned music_trackcount() { return visible_music().music_trackcount(); }
unsigned music_currenttrack() { return visible_music().music_currenttrack(); }
std::string music_tracktitle(unsigned index) { return visible_music().music_tracktitle(index); }
std::string music_title() { return visible_music().music_title(); }
uint32_t music_getduration() { return visible_music().music_getduration(); }
uint32_t music_getposition() { return visible_music().music_getposition(); }
const std::string &music_error() { return visible_music().music_error(); }
const visualization_data &music_visualization() { return visible_music().music_visualization(); }
void music_run()
{
    music_poll();
    visible_music().music_run();
}
