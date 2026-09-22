#include "xsf_loader.h"
#include "replays/vio2sf/desmume/state.h"
namespace
{
    struct twosf_engine final : xsf_engine
    {
        NDS_state state{};
        std::vector<uint8_t> rom, saved;
        bool initialized = false;
        ~twosf_engine() override
        {
            if (initialized)
                state_deinit(&state);
        }
        bool accepts(uint8_t v) const override { return v == 0x24; }
        bool reset(unsigned) override
        {
            if (initialized)
            {
                state_deinit(&state);
                initialized = false;
            }
            state = {};
            rom.clear();
            saved.clear();
            for (auto &s : file.sections)
            {
                if (!s.exe.empty() && !xsf_map(rom, s.exe.data(), s.exe.size(), 128 * 1024 * 1024, true))
                    return false;
                size_t p = 0;
                while (p < s.reserved.size())
                {
                    if (s.reserved.size() - p < 12)
                        return false;
                    auto *b = s.reserved.data() + p;
                    size_t n = replay_le32(b + 4);
                    if (n > s.reserved.size() - p - 12)
                        return false;
                    if (!memcmp(b, "SAVE", 4))
                    {
                        auto data = inflate_xsf(b + 12, n);
                        if (!xsf_map(saved, data.data(), data.size(), 64 * 1024 * 1024))
                            return false;
                    }
                    p += 12 + n;
                }
            }
            if (rom.empty() || state_init(&state))
                return false;
            initialized = true;
            state.dwInterpolation = 0;
            state.initial_frames = std::clamp(tag_number("_frames", -1), -1, 36000);
            state.sync_type = std::clamp(tag_number("_vio2sf_sync_type"), 0, 2);
            const int clockdown = std::clamp(tag_number("_clockdown"), 0, 8);
            state.arm7_clockdown_level = std::clamp(tag_number("_vio2sf_arm7_clockdown_level", clockdown), 0, 8);
            state.arm9_clockdown_level = std::clamp(tag_number("_vio2sf_arm9_clockdown_level", clockdown), 0, 8);
            state_setrom(&state, rom.data(), uint32_t(rom.size()), 0);
            state_loadstate(&state, saved.data(), uint32_t(saved.size()));
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            std::array<int16_t, 1024> pcm{};
            state_render(&state, pcm.data(), 512);
            replay_pcm16(out, pcm.data(), 512);
            return true;
        }
    };
}
std::unique_ptr<replay_engine> make_2sf_engine() { return std::make_unique<twosf_engine>(); }
