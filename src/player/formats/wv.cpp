#include "decoder_base.h"
#include <wavpack/wavpack.h>
#include <cmath>
#include <cstring>
class wavpack_decoder final : public decoder_base
{
    WavpackContext *stream_ = nullptr;
    struct memory_file
    {
        std::vector<uint8_t> bytes;
        size_t cursor = 0;
    } main_, correction_;
    static int32_t read(void *id, void *data, int32_t wanted)
    {
        auto &file = *static_cast<memory_file *>(id);
        const size_t count = wanted > 0 ? std::min<size_t>(wanted, file.bytes.size() - file.cursor) : 0;
        if (count)
            std::memcpy(data, file.bytes.data() + file.cursor, count);
        file.cursor += count;
        return static_cast<int32_t>(count);
    }
    static int64_t tell(void *id) { return static_cast<memory_file *>(id)->cursor; }
    static int seek_bytes(void *id, int64_t offset)
    {
        auto &file = *static_cast<memory_file *>(id);
        if (offset < 0 || uint64_t(offset) > file.bytes.size())
            return -1;
        file.cursor = size_t(offset);
        return 0;
    }
    static int seek_relative(void *id, int64_t delta, int mode)
    {
        const auto &file = *static_cast<memory_file *>(id);
        const int64_t base = mode == SEEK_SET ? 0 : mode == SEEK_CUR ? int64_t(file.cursor)
                                                                     : int64_t(file.bytes.size());
        if ((delta > 0 && base > INT64_MAX - delta) || delta < -base)
            return -1;
        return seek_bytes(id, base + delta);
    }
    static int push_back(void *id, int value)
    {
        auto &file = *static_cast<memory_file *>(id);
        if (!file.cursor)
            return EOF;
        --file.cursor;
        return value;
    }
    static int64_t length(void *id) { return static_cast<memory_file *>(id)->bytes.size(); }
    static int can_seek(void *) { return 1; }
    std::vector<int32_t> samples_;
    size_t read_frames(float *out, size_t frames) override
    {
        samples_.resize(frames * channels_);
        size_t got = WavpackUnpackSamples(stream_, samples_.data(), static_cast<uint32_t>(frames));
        if (WavpackGetMode(stream_) & MODE_FLOAT)
            std::memcpy(out, samples_.data(), got * channels_ * sizeof(float));
        else
        {
            const float scale = std::ldexp(1.0f, 1 - WavpackGetBitsPerSample(stream_));
            for (size_t i = 0; i < got * channels_; ++i)
                out[i] = samples_[i] * scale;
        }
        return got;
    }
    bool seek_frame(uint64_t frame) override { return stream_ && WavpackSeekSample64(stream_, frame); }

public:
    ~wavpack_decoder() override { stop(); }
    std::vector<std::string> file_types() override { return {"wv"}; }
    bool open(const char *filename, float *rate, bool loop) override
    {
        stop();
        char error[128] = {};
        main_.bytes = read_audio_file(filename);
        if (main_.bytes.empty())
            return false;
        correction_.bytes = read_audio_file((std::string(filename) + "c").c_str());
        static WavpackStreamReader64 reader = {read, nullptr, tell, seek_bytes, seek_relative,
                                               push_back, length, can_seek, nullptr, nullptr};
        stream_ = WavpackOpenFileInputEx64(&reader, &main_, correction_.bytes.empty() ? nullptr : &correction_,
                                           error, OPEN_WVC | OPEN_NORMALIZE | OPEN_DSD_AS_PCM, 0);
        if (!stream_)
            return false;
        rate_ = WavpackGetSampleRate(stream_);
        channels_ = WavpackGetNumChannels(stream_);
        if (!channels_ || channels_ > 8 || !rate_)
        {
            stop();
            return false;
        }
        const int64_t total = WavpackGetNumSamples64(stream_);
        length_ = total > 0 ? total : 0;
        position_ = 0;
        *rate = float(rate_);
        loop_ = loop;
        playing_ = true;
        read_tags(main_.bytes);
        return true;
    }
    void stop() override
    {
        metadata_.clear();
        if (stream_)
            WavpackCloseFile(stream_);
        stream_ = nullptr;
        main_ = {};
        correction_ = {};
        playing_ = false;
    }
};
auddecode *create_wv() { return new wavpack_decoder; }
