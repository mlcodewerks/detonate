#include "xsf_loader.h"
#include "replays/LazyUSF/lazyusf2/usf/usf.h"
namespace
{
    struct usf_engine final : xsf_engine
    {
        std::vector<uint8_t> state;
        ~usf_engine() override
        {
            if (!state.empty())
                usf_shutdown(state.data());
        }
        bool accepts(uint8_t v) const override { return v == 0x21; }
        bool reset(unsigned) override
        {
            if (!state.empty())
                usf_shutdown(state.data());
            state.resize(usf_get_state_size());
            usf_clear(state.data());
            for (auto &s : file.sections)
            {
                if (!s.exe.empty() || usf_upload_section(state.data(), s.reserved.data(), s.reserved.size()))
                    return false;
            }
            usf_set_hle_audio(state.data(), 1);
            usf_set_compare(state.data(), file.tags.count("_enablecompare") != 0);
            usf_set_fifo_full(state.data(), file.tags.count("_enablefifofull") != 0);
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            std::array<int16_t, 1024> pcm{};
            if (usf_render_resampled(state.data(), pcm.data(), 512, rate))
                return false;
            replay_pcm16(out, pcm.data(), 512);
            return true;
        }
    };
}
std::unique_ptr<replay_engine> make_usf_engine() { return std::make_unique<usf_engine>(); }
