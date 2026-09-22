#pragma once
#include "game_music_io.h"
#include <memory>
#include <cstring>
#include <array>

struct replay_engine
{
    unsigned rate = 44100, duration = 180000, tracks = 1, track = 0;
    std::string title, artist, album;
    virtual ~replay_engine() = default;
    virtual bool load(const char *path) = 0;
    virtual bool load_memory(const std::string &, const std::vector<uint8_t> &) { return false; }
    virtual bool reset(unsigned subsong) = 0;
    virtual bool render(std::vector<float> &pcm) = 0;
};

inline void replay_pcm16(std::vector<float> &out, const int16_t *in, size_t frames)
{
    out.resize(frames * 2);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = in[i] / 32768.0f;
}
inline uint32_t replay_le32(const uint8_t *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

class replay_decoder final : public decoder_base
{
    std::unique_ptr<replay_engine> engine_;
    std::vector<std::string> types_;
    std::vector<float> block_;
    size_t offset_ = 0;
    size_t read_frames(float *out, size_t frames) override
    {
        if (!loop_)
        {
            if (position_ >= length_)
                return 0;
            frames = std::min<uint64_t>(frames, length_ - position_);
        }
        if (!frames)
            return 0;
        if (offset_ == block_.size())
        {
            block_.clear();
            offset_ = 0;
            if (!engine_->render(block_) || block_.empty() || block_.size() % 2)
                return 0;
        }
        const size_t count = std::min(frames, (block_.size() - offset_) / 2);
        std::copy_n(block_.data() + offset_, count * 2, out);
        offset_ += count * 2;
        return count;
    }
    bool seek_frame(uint64_t frame) override
    {
        if (!rate_ || !engine_->reset(engine_->track))
            return false;
        block_.clear();
        offset_ = 0;
        while (frame)
        {
            if (!engine_->render(block_) || block_.empty())
                return false;
            const auto skip = std::min<uint64_t>(frame, block_.size() / 2);
            frame -= skip;
            offset_ = size_t(skip) * 2;
        }
        return true;
    }

public:
    replay_decoder(std::unique_ptr<replay_engine> engine, std::vector<std::string> types)
        : engine_(std::move(engine)), types_(std::move(types)) {}
    bool open(const char *path, float *rate, bool loop) override
    {
        stop();
        if (!path || !rate || !engine_->load(path))
            return false;
        rate_ = engine_->rate;
        channels_ = 2;
        loop_ = loop;
        if (!select_track(engine_->track))
        {
            stop();
            return false;
        }
        *rate = float(rate_);
        return true;
    }
    bool open_memory(const std::string &name, const std::vector<uint8_t> &bytes, float *rate, bool loop) override
    {
        stop();
        if (!rate || !engine_->load_memory(name, bytes))
            return false;
        rate_ = engine_->rate;
        channels_ = 2;
        loop_ = loop;
        if (!select_track(engine_->track))
        {
            stop();
            return false;
        }
        *rate = float(rate_);
        return true;
    }
    void stop() override
    {
        engine_->title.clear();
        engine_->artist.clear();
        engine_->album.clear();
        playing_ = false;
        rate_ = channels_ = 0;
        position_ = length_ = 0;
        block_.clear();
        offset_ = 0;
    }
    bool supports_looping() const override { return true; }
    std::vector<std::string> file_types() override { return types_; }
    const char *song_title() override { return engine_->title.c_str(); }
    const char *song_artist() override { return engine_->artist.c_str(); }
    const char *song_album() override { return engine_->album.c_str(); }
    unsigned track_count() const override { return engine_->tracks; }
    unsigned current_track() const override { return engine_->track; }
    std::string track_title(unsigned i) const override
    {
        return i < track_count() ? "Track " + std::to_string(i + 1) : "";
    }
    bool select_track(unsigned i) override
    {
        if (!rate_ || i >= engine_->tracks || !engine_->reset(i))
            return false;
        engine_->track = i;
        length_ = uint64_t(engine_->duration) * rate_ / 1000;
        block_.clear();
        offset_ = 0;
        position_ = 0;
        playing_ = true;
        return true;
    }
};
