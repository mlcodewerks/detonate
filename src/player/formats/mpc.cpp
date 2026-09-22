#include "decoder_base.h"
#include "musepack/mpcdec.h"
#include "musepack/reader.h"
#include <cstring>
class musepack_decoder final : public decoder_base
{
    mpc_reader reader_ = {};
    mpc_demux *demux_ = nullptr;
    std::vector<uint8_t> bytes_;
    size_t cursor_ = 0;
    static musepack_decoder &self(mpc_reader *r) { return *static_cast<musepack_decoder *>(r->data); }
    static mpc_int32_t read(mpc_reader *r, void *out, mpc_int32_t size)
    {
        auto &s = self(r);
        const size_t count = size > 0 ? std::min<size_t>(size, s.bytes_.size() - s.cursor_) : 0;
        std::memcpy(out, s.bytes_.data() + s.cursor_, count);
        s.cursor_ += count;
        return static_cast<mpc_int32_t>(count);
    }
    static mpc_bool_t seek_bytes(mpc_reader *r, mpc_int32_t offset)
    {
        auto &s = self(r);
        if (offset < 0 || size_t(offset) > s.bytes_.size())
            return MPC_FALSE;
        s.cursor_ = offset;
        return MPC_TRUE;
    }
    static mpc_int32_t tell(mpc_reader *r) { return static_cast<mpc_int32_t>(self(r).cursor_); }
    static mpc_int32_t size(mpc_reader *r) { return static_cast<mpc_int32_t>(self(r).bytes_.size()); }
    static mpc_bool_t canseek(mpc_reader *) { return MPC_TRUE; }
    float packet_[MPC_DECODER_BUFFER_LENGTH] = {};
    size_t pending_ = 0, offset_ = 0;
    size_t read_frames(float *out, size_t frames) override
    {
        if (!pending_)
        {
            mpc_frame_info info = {};
            info.buffer = packet_;
            // Some stream headers yield empty packets. Bound retries on damaged input.
            unsigned attempts = 0;
            do
            {
                if (++attempts > 64 || mpc_demux_decode(demux_, &info) != MPC_STATUS_OK || info.bits == -1)
                    return 0;
            } while (!info.samples);
            pending_ = info.samples;
            offset_ = 0;
        }
        size_t got = std::min(frames, pending_);
        std::memcpy(out, packet_ + offset_ * channels_, got * channels_ * sizeof(float));
        offset_ += got;
        pending_ -= got;
        return got;
    }
    bool seek_frame(uint64_t frame) override
    {
        if (!demux_ || mpc_demux_seek_sample(demux_, frame) != MPC_STATUS_OK)
            return false;
        pending_ = offset_ = 0;
        return true;
    }

public:
    ~musepack_decoder() override { stop(); }
    std::vector<std::string> file_types() override { return {"mpc", "mpp", "mp+"}; }
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
        if (bytes_.empty() || bytes_.size() > INT32_MAX)
            return false;
        cursor_ = 0;
        reader_ = {read, seek_bytes, tell, size, canseek, this};
        demux_ = mpc_demux_init(&reader_);
        if (!demux_)
        {
            stop();
            return false;
        }
        mpc_streaminfo info = {};
        mpc_demux_get_info(demux_, &info);
        rate_ = info.sample_freq;
        channels_ = info.channels;
        if (!rate_ || !channels_ || channels_ > 2)
        {
            stop();
            return false;
        }
        const auto total = mpc_streaminfo_get_length_samples(&info);
        length_ = total > 0 ? total : 0;
        position_ = 0;
        *rate = float(rate_);
        loop_ = loop;
        playing_ = true;
        read_tags(bytes_);
        return true;
    }
    void stop() override
    {
        metadata_.clear();
        if (demux_)
            mpc_demux_exit(demux_);
        demux_ = nullptr;
        bytes_.clear();
        cursor_ = 0;
        reader_ = {};
        playing_ = false;
        pending_ = offset_ = 0;
    }
};
auddecode *create_mpc() { return new musepack_decoder; }
