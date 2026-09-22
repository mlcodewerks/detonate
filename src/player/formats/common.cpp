#include "decoder_base.h"
#include <formats/audio.h>
#include <cctype>

class common_decoder final : public decoder_base
{
    void *decoder_ = nullptr;
    audio_type_enum type_ = AUDIO_TYPE_NONE;
    std::vector<uint8_t> bytes_;
    size_t read_frames(float *out, size_t frames) override
    {
        // The tracker follows its own restart orders and pattern jumps. Do
        // not rewind it at the estimated first-pass duration when looping.
        if (type_ == AUDIO_TYPE_MOD && length_ && !loop_)
        {
            if (position_ >= length_)
                return 0;
            frames = std::min<uint64_t>(frames, length_ - position_);
        }
        size_t got = 0;
        const int result = audio_transfer_read_f32(decoder_, type_, out, frames, &got);
        return result < 0 ? 0 : got;
    }
    bool seek_frame(uint64_t frame) override
    {
        return decoder_ && audio_transfer_seek(decoder_, type_, frame);
    }
    bool vorbis_order() const override
    {
        return type_ == AUDIO_TYPE_VORBIS || type_ == AUDIO_TYPE_OPUS;
    }

public:
    ~common_decoder() override { stop(); }
    bool is_module() const override { return type_ == AUDIO_TYPE_MOD; }
    std::vector<std::string> file_types() override
    {
        return {"wav", "flac", "fla", "mp3", "ogg", "oga", "opus", "aac", "m4a", "mod", "s3m", "xm", "it"};
    }
    bool open(const char *filename, float *rate, bool loop) override
    {
        return open_bytes(filename, read_audio_file(filename), rate, loop);
    }
    bool open_memory(const std::string &name, const std::vector<uint8_t> &bytes, float *rate, bool loop) override
    {
        return open_bytes(name.c_str(), bytes, rate, loop);
    }
    bool open_bytes(const char *filename, std::vector<uint8_t> bytes, float *rate, bool loop)
    {
        stop();
        bytes_ = std::move(bytes);
        if (bytes_.empty())
            return false;
        std::string ext = std::filesystem::path(reinterpret_cast<const char8_t *>(filename)).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                       { return std::tolower(c); });
        if (ext == ".ogg" || ext == ".oga" || ext == ".opus")
            type_ = audio_transfer_ogg_audio_type(bytes_.data(), bytes_.size());
        else if (ext == ".aac" || ext == ".m4a")
            type_ = AUDIO_TYPE_AAC;
        else if (ext == ".fla")
            type_ = AUDIO_TYPE_FLAC;
        else if (ext == ".it")
            type_ = AUDIO_TYPE_MOD;
        else
            type_ = audio_decode_get_type(filename);
        decoder_ = audio_transfer_new(type_);
        if (!decoder_)
        {
            stop();
            return false;
        }
        audio_transfer_set_buffer_ptr(decoder_, type_, bytes_.data(), bytes_.size());
        audio_transfer_set_output_rate(decoder_, type_, 44100);
        if (!audio_transfer_start(decoder_, type_) ||
            !audio_transfer_info(decoder_, type_, &channels_, &rate_, &length_) ||
            !channels_ || channels_ > 8 || rate_ < 8000 || rate_ > 384000)
        {
            stop();
            return false;
        }
        position_ = 0;
        *rate = static_cast<float>(rate_);
        loop_ = loop;
        playing_ = true;
        read_tags(bytes_);
        return true;
    }
    void stop() override
    {
        if (decoder_)
            audio_transfer_free(decoder_, type_);
        decoder_ = nullptr;
        bytes_.clear();
        metadata_.clear();
        playing_ = false;
        rate_ = channels_ = 0;
        length_ = position_ = 0;
    }
};
auddecode *create_common() { return new common_decoder; }
