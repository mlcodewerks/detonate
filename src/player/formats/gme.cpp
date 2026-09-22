#include "game_music_io.h"
#include "gme/gme/gme.h"
#include "archive_reader.h"
#include <climits>
#include <cstring>
#include <memory>

namespace
{
    using emulator = std::unique_ptr<Music_Emu, decltype(&gme_delete)>;
    using spc_entry = archive_member;

    std::vector<spc_entry> read_rsn(const std::vector<uint8_t> &bytes)
    {
        auto members = read_music_archive(bytes, "archive.rsn", 16 * 1024 * 1024);
        std::vector<spc_entry> entries;
        for (auto &member : members)
        {
            if (game_music_extension(member.name) == "spc" && member.bytes.size() >= 0x10180 &&
                !std::memcmp(member.bytes.data(), "SNES-SPC700 Sound File Data", 26))
            {
                if (entries.size() >= 4096)
                    throw std::runtime_error("RSN contains too many SPC tracks.");
                entries.push_back(std::move(member));
            }
        }
        if (entries.empty())
            throw std::runtime_error("RSN archive contains no valid SPC tracks.");
        std::stable_sort(entries.begin(), entries.end(), [](const auto &a, const auto &b)
                         { return a.name < b.name; });
        return entries;
    }
}
class gme_decoder final : public decoder_base
{
    emulator emu_{nullptr, gme_delete};
    gme_type_t type_ = nullptr;
    std::vector<uint8_t> bytes_;
    std::vector<spc_entry> entries_;
    std::vector<std::string> names_;
    std::vector<int16_t> pcm_;
    unsigned track_ = 0;
    std::string title_;

    emulator load(const std::vector<uint8_t> &bytes)
    {
        if (bytes.size() > LONG_MAX)
            return emulator(nullptr, gme_delete);
        Music_Emu *raw = gme_new_emu(type_, 44100);
        emulator result(raw, gme_delete);
        if (!result || gme_load_data(raw, bytes.data(), long(bytes.size())))
            return emulator(nullptr, gme_delete);
        gme_set_autoload_playback_limit(raw, 0);
        gme_ignore_silence(raw, 1);
        return result;
    }
    bool start(Music_Emu *emu, unsigned index, unsigned display_track)
    {
        if (gme_start_track(emu, int(index)))
            return false;
        gme_set_fade_msecs(emu, -1, 0);
        gme_info_t *raw = nullptr;
        if (gme_track_info(emu, &raw, int(index)))
            return false;
        std::unique_ptr<gme_info_t, decltype(&gme_free_info)> info(raw, gme_free_info);
        title_ = info->song;
        metadata_.artist = info->author;
        metadata_.album = info->game;
        if (title_.empty())
            title_ = names_[display_track];
        const unsigned duration = info->play_length > 0 ? unsigned(info->play_length) : 150000;
        length_ = uint64_t(duration) * rate_ / 1000;
        position_ = 0;
        playing_ = true;
        track_ = display_track;
        return true;
    }
    size_t read_frames(float *out, size_t frames) override
    {
        if (loop_ && gme_tell_samples(emu_.get()) > INT_MAX - 65536 && !seek_frame(0))
            return 0;
        if (!loop_)
        {
            if (position_ >= length_)
                return 0;
            frames = std::min<uint64_t>(frames, length_ - position_);
        }
        if (gme_track_ended(emu_.get()))
            return 0;
        frames = std::min<size_t>(frames, 4096);
        pcm_.resize(frames * 2);
        if (gme_play(emu_.get(), int(frames * 2), pcm_.data()))
            return 0;
        for (size_t i = 0; i < frames * 2; ++i)
            out[i] = pcm_[i] / 32768.0f;
        return frames;
    }
    bool seek_frame(uint64_t frame) override
    {
        if (!emu_ || frame > INT_MAX / 2)
            return false;
        gme_ignore_silence(emu_.get(), 1);
        const auto result = gme_seek_samples(emu_.get(), int(frame * 2));
        if (result)
            return false;
        gme_set_fade_msecs(emu_.get(), -1, 0);
        return true;
    }

public:
    ~gme_decoder() override { stop(); }
    bool supports_looping() const override { return true; }
    void set_loop(bool loop) override
    {
        decoder_base::set_loop(loop);
        if (emu_)
        {
            gme_set_autoload_playback_limit(emu_.get(), 0);
            gme_ignore_silence(emu_.get(), 1);
            gme_set_fade_msecs(emu_.get(), -1, 0);
        }
    }
    std::vector<std::string> file_types() override { return {"ay", "gbs", "gym", "hes", "kss", "nsf", "nsfe", "sap", "spc", "rsn"}; }
    const char *song_title() override { return title_.c_str(); }
    unsigned track_count() const override { return static_cast<unsigned>(names_.size()); }
    unsigned current_track() const override { return track_; }
    std::string track_title(unsigned index) const override { return index < names_.size() ? names_[index] : ""; }
    bool select_track(unsigned index) override
    {
        if (index >= names_.size())
            return false;
        auto next = load(entries_.empty() ? bytes_ : entries_[index].bytes);
        if (!next || !start(next.get(), entries_.empty() ? index : 0, index))
            return false;
        emu_ = std::move(next);
        set_loop(loop_);
        return true;
    }
    bool open(const char *filename, float *rate, bool loop) override
    {
        stop();
        auto bytes = read_game_music(filename);
        rate_ = 44100;
        channels_ = 2;
        loop_ = loop;
        if (game_music_extension(filename) == "rsn")
        {
            type_ = gme_spc_type;
            entries_ = read_rsn(bytes);
            for (const auto &entry : entries_)
                names_.push_back(entry.name);
        }
        else
        {
            type_ = gme_identify_extension(filename);
            if (!type_)
            {
                stop();
                return false;
            }
            bytes_ = std::move(bytes);
            emu_ = load(bytes_);
            if (!emu_)
            {
                stop();
                return false;
            }
            const int count = gme_track_count(emu_.get());
            if (count <= 0 || count > 4096)
            {
                stop();
                return false;
            }
            for (int i = 0; i < count; ++i)
            {
                gme_info_t *info = nullptr;
                if (gme_track_info(emu_.get(), &info, i))
                {
                    stop();
                    return false;
                }
                names_.push_back(info->song[0] ? info->song : "Track " + std::to_string(i + 1));
                gme_free_info(info);
            }
        }
        if (!select_track(0))
        {
            stop();
            return false;
        }
        *rate = float(rate_);
        return true;
    }
    void stop() override
    {
        emu_.reset();
        type_ = nullptr;
        bytes_.clear();
        entries_.clear();
        names_.clear();
        pcm_.clear();
        title_.clear();
        metadata_.clear();
        track_ = 0;
        playing_ = loop_ = false;
        rate_ = channels_ = 0;
        length_ = position_ = 0;
    }
};

auddecode *create_gme() { return new gme_decoder; }
