#include "../replayer_settings.h"
#include "decoder_base.h"
#include <libopenmpt/libopenmpt.hpp>
#include <cmath>
#include <sstream>

namespace
{
    class openmpt_decoder final : public decoder_base
    {
        std::ostringstream log_;
        std::unique_ptr<openmpt::module> module_;
        unsigned track_ = 0;
        std::vector<std::string> names_;

        void apply_settings()
        {
            const auto s = replayer_settings::snapshot();
            constexpr int interpolation[] = {0, 1, 2, 4, 8};
            module_->set_render_param(openmpt::module::RENDER_INTERPOLATIONFILTER_LENGTH, interpolation[s[replayer_settings::mpt_interpolation]]);
            module_->set_render_param(openmpt::module::RENDER_VOLUMERAMPING_STRENGTH, s[replayer_settings::mpt_ramping]);
            module_->set_render_param(openmpt::module::RENDER_STEREOSEPARATION_PERCENT, s[replayer_settings::mpt_separation]);
            module_->set_render_param(openmpt::module::RENDER_MASTERGAIN_MILLIBEL, s[replayer_settings::mpt_gain] * 100);
        }
        size_t read_frames(float *out, size_t frames) override
        {
            if (!module_)
                return 0;
            if (!loop_ && length_)
            {
                if (position_ >= length_)
                    return 0;
                frames = std::min<uint64_t>(frames, length_ - position_);
            }
            return module_->read_interleaved_stereo(rate_, frames, out);
        }
        bool seek_frame(uint64_t frame) override
        {
            if (!module_)
                return false;
            apply_settings();
            module_->set_position_seconds(double(frame) / rate_);
            return true;
        }

    public:
        ~openmpt_decoder() override { stop(); }
        bool is_module() const override { return true; }
        std::vector<std::string> file_types() override
        {
            return openmpt::get_supported_extensions();
        }
        bool open(const char *path, float *rate, bool loop) override
        {
            return path && open_memory(path, read_audio_file(path), rate, loop);
        }
        bool open_memory(const std::string &, const std::vector<uint8_t> &bytes, float *rate, bool loop) override
        {
            stop();
            if (!rate || bytes.empty())
                return false;
            try
            {
                module_ = std::make_unique<openmpt::module>(bytes, log_,
                    std::map<std::string, std::string>{{"seek.sync_samples", "1"}});
                names_ = module_->get_subsong_names();
                metadata_.title = module_->get_metadata("title");
                metadata_.artist = module_->get_metadata("artist");
                rate_ = 44100;
                channels_ = 2;
                loop_ = loop;
                module_->set_repeat_count(loop ? -1 : 0);
                if (!select_track(0))
                {
                    stop();
                    return false;
                }
                *rate = float(rate_);
                return true;
            }
            catch (const std::exception &)
            {
                stop();
                return false;
            }
        }
        void stop() override
        {
            module_.reset();
            names_.clear();
            metadata_.clear();
            log_.str({});
            log_.clear();
            track_ = rate_ = channels_ = 0;
            length_ = position_ = 0;
            playing_ = false;
        }
        void set_loop(bool loop) override
        {
            if (module_)
            {
                module_->set_repeat_count(loop ? -1 : 0);
                if (loop && !playing_)
                {
                    module_->set_position_seconds(0);
                    position_ = 0;
                }
            }
            decoder_base::set_loop(loop);
        }
        unsigned track_count() const override
        {
            return module_ ? unsigned(module_->get_num_subsongs()) : 0;
        }
        unsigned current_track() const override { return track_; }
        std::string track_title(unsigned i) const override
        {
            if (i >= track_count())
                return {};
            return i < names_.size() && !names_[i].empty() ? names_[i] : "Track " + std::to_string(i + 1);
        }
        bool select_track(unsigned i) override
        {
            if (!module_ || i >= track_count())
                return false;
            apply_settings();
            module_->select_subsong(int32_t(i));
            module_->set_position_seconds(0);
            const double seconds = module_->get_duration_seconds();
            if (!std::isfinite(seconds) || seconds <= 0 || seconds > double(UINT32_MAX) / 1000)
                return false;
            length_ = uint64_t(seconds * rate_);
            track_ = i;
            position_ = 0;
            playing_ = true;
            return true;
        }
    };
}

auddecode *create_openmpt() { return new openmpt_decoder; }
