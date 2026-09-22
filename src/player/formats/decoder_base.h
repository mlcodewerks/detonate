#pragma once
#include "audiodecode.h"
#include "audiotags.hpp"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <limits>

inline std::vector<uint8_t> read_audio_file(const char *filename)
{
    std::ifstream file(std::filesystem::path(reinterpret_cast<const char8_t *>(filename)), std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    const auto length = file.tellg();
    if (length <= 0 || uint64_t(length) > std::numeric_limits<size_t>::max())
        return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), length))
        return {};
    return bytes;
}

// Shared frame accounting, channel conversion, and bounded looping.
class decoder_base : public auddecode
{
protected:
    audiotags::Metadata metadata_;
    // Tags are optional; malformed metadata must not prevent audio playback.
    void read_tags(const std::vector<uint8_t> &bytes)
    {
        metadata_.clear();
        try
        {
            audiotags::read_memory(bytes.data(), bytes.size(), metadata_);
        }
        catch (const std::exception &)
        {
            metadata_.clear();
        }
        metadata_.pictures.clear();
    }
    void read_tags(const char *filename)
    {
        metadata_.clear();
        try
        {
            audiotags::read_file(filename, metadata_);
        }
        catch (const std::exception &)
        {
            metadata_.clear();
        }
        metadata_.pictures.clear();
    }
    unsigned rate_ = 0, channels_ = 0;
    uint64_t length_ = 0, position_ = 0;
    bool playing_ = false, loop_ = false;
    std::vector<float> native_, stereo_;
    virtual size_t read_frames(float *out, size_t frames) = 0;
    virtual bool seek_frame(uint64_t frame) = 0;
    virtual bool vorbis_order() const { return false; }

public:
    const char *song_title() override { return metadata_.title.c_str(); }
    const char *song_artist() override { return metadata_.artist.c_str(); }
    const char *song_album() override { return metadata_.album.c_str(); }
    bool is_playing() override { return playing_; }
    void set_loop(bool loop) override
    {
        loop_ = loop;
        if (loop && rate_ && channels_)
            playing_ = true;
    }
    unsigned song_duration() override
    {
        return rate_ ? static_cast<unsigned>(std::min<uint64_t>(length_ * 1000 / rate_, UINT32_MAX)) : 0;
    }
    bool seek(unsigned ms) override
    {
        uint64_t frame = uint64_t(ms) * rate_ / 1000;
        if (length_)
            frame = std::min(frame, length_);
        if (!seek_frame(frame))
            return false;
        position_ = frame;
        playing_ = true;
        return true;
    }
    void mix(float *&buffer, unsigned &frames) override
    {
        const unsigned wanted = frames;
        frames = 0;
        buffer = nullptr;
        if (!playing_ || !wanted || !channels_ || channels_ > 8)
            return;
        native_.resize(size_t(wanted) * channels_);
        stereo_.resize(size_t(wanted) * 2);
        bool rewound_without_audio = false;
        while (frames < wanted)
        {
            const size_t got = read_frames(native_.data() + size_t(frames) * channels_, wanted - frames);
            if (!got)
            {
                if (!loop_ || rewound_without_audio || !seek_frame(0))
                {
                    playing_ = false;
                    break;
                }
                position_ = 0;
                rewound_without_audio = true;
                continue;
            }
            rewound_without_audio = false;
            frames += static_cast<unsigned>(got);
            position_ += got;
        }
        for (unsigned i = 0; i < frames; ++i)
        {
            const float *s = native_.data() + size_t(i) * channels_;
            float l = s[0], r = channels_ == 1 ? s[0] : s[1];
            if (channels_ > 2)
            {
                // WAV: L R C LFE BL BR SL SR; Vorbis/Opus: L C R
                // surround channels, LFE. Quad is L R BL BR in both.
                const bool vorbis = vorbis_order() && channels_ != 4;
                r = s[vorbis ? 2 : 1];
                float weight = 1.0f;
                if (channels_ != 4)
                {
                    l += s[vorbis ? 1 : 2] * 0.70710678f;
                    r += s[vorbis ? 1 : 2] * 0.70710678f;
                    weight += 0.70710678f;
                }
                unsigned start = channels_ == 4 ? 2 : 3;
                unsigned end = channels_;
                if (channels_ >= 6)
                {
                    const unsigned lfe = vorbis ? channels_ - 1 : 3;
                    l += s[lfe] * 0.5f;
                    r += s[lfe] * 0.5f;
                    weight += 0.5f;
                    if (vorbis)
                        --end;
                    else
                        ++start;
                }
                if (!vorbis && channels_ == 7)
                {
                    l += s[4] * 0.5f;
                    r += s[4] * 0.5f;
                    weight += 0.5f;
                    start = 5;
                }
                for (unsigned c = start; c < end; c += 2)
                {
                    l += s[c] * 0.70710678f;
                    r += s[c + 1 < end ? c + 1 : c] * 0.70710678f;
                    weight += 0.70710678f;
                }
                l /= weight;
                r /= weight;
            }
            stereo_[size_t(i) * 2] = l;
            stereo_[size_t(i) * 2 + 1] = r;
        }
        buffer = stereo_.data();
    }
};
