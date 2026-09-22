// libretro-common's rwav handles RIFF/WAVE. Retain Wave64 through dr_wav.
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "decoder_base.h"
class wave64_decoder final : public decoder_base
{
    drwav stream_ = {};
    bool opened_ = false;
    std::vector<uint8_t> bytes_;
    size_t read_frames(float *out, size_t frames) override
    {
        return static_cast<size_t>(drwav_read_pcm_frames_f32(&stream_, frames, out));
    }
    bool seek_frame(uint64_t frame) override
    {
        return opened_ && drwav_seek_to_pcm_frame(&stream_, frame);
    }

public:
    ~wave64_decoder() override { stop(); }
    std::vector<std::string> file_types() override { return {"w64"}; }
    bool open(const char *filename, float *rate, bool loop) override
    {
        return open_bytes(filename, read_audio_file(filename), rate, loop);
    }
    bool open_memory(const std::string &name, const std::vector<uint8_t> &bytes, float *rate, bool loop) override
    {
        return open_bytes(name.c_str(), bytes, rate, loop);
    }
    bool open_bytes(const char *, std::vector<uint8_t> bytes, float *rate, bool loop)
    {
        stop();
        bytes_ = std::move(bytes);
        opened_ = drwav_init_memory(&stream_, bytes_.data(), bytes_.size(), nullptr);
        if (!opened_)
            return false;
        rate_ = stream_.sampleRate;
        channels_ = stream_.channels;
        if (!rate_ || !channels_ || channels_ > 8)
        {
            stop();
            return false;
        }
        length_ = stream_.totalPCMFrameCount;
        position_ = 0;
        *rate = float(rate_);
        playing_ = true;
        loop_ = loop;
        read_tags(bytes_);
        return true;
    }
    void stop() override
    {
        metadata_.clear();
        if (opened_)
            drwav_uninit(&stream_);
        opened_ = playing_ = false;
        bytes_.clear();
    }
};
auddecode *create_wav() { return new wave64_decoder; }
