#include "xsf_loader.h"
#include <mutex>
#include "replays/HighlyTheoretical/core/sega.h"
namespace
{
    struct sega_engine final : xsf_engine
    {
        std::vector<uint8_t> state;
        bool accepts(uint8_t v) const override { return v == 0x11 || v == 0x12; }
        bool reset(unsigned) override
        {
            static std::once_flag once;
            std::call_once(once, []
                           { sega_init(); });
            state.resize(sega_get_state_size(file.version - 0x10));
            sega_clear_state(state.data(), file.version - 0x10);
            sega_enable_dry(state.data(), 1);
            sega_enable_dsp(state.data(), 1);
            sega_enable_dsp_dynarec(state.data(), 0);
            const size_t max = file.version == 0x12 ? 0x800000 : 0x80000;
            bool loaded = false;
            for (auto &s : file.sections)
                if (!s.exe.empty())
                {
                    if (s.exe.size() < 4)
                        return false;
                    size_t addr = replay_le32(s.exe.data());
                    if (addr >= max || s.exe.size() - 4 > max - addr)
                        return false;
                    if (sega_upload_program(state.data(), s.exe.data(), uint32_t(s.exe.size())))
                        return false;
                    loaded = true;
                }
            return loaded;
        }
        bool render(std::vector<float> &out) override
        {
            std::array<int16_t, 1024> pcm{};
            uint32_t n = 512;
            if (sega_execute(state.data(), 0x7fffffff, pcm.data(), &n) < 0 || !n)
                return false;
            replay_pcm16(out, pcm.data(), n);
            return true;
        }
    };
}
std::unique_ptr<replay_engine> make_sega_engine() { return std::make_unique<sega_engine>(); }
