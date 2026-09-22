#include "xsf_loader.h"
#include <mutex>
#include "replays/HighlyQuixotic/highly_quixotic/qsound.h"
namespace
{
    struct qsf_engine final : xsf_engine
    {
        std::vector<uint8_t> state, key, rom, samples;
        qsf_engine() { rate = 24038; }
        bool accepts(uint8_t v) const override { return v == 0x41; }
        bool reset(unsigned) override
        {
            static std::once_flag once;
            std::call_once(once, []
                           { qsound_init(); });
            key.clear();
            rom.clear();
            samples.clear();
            for (auto &s : file.sections)
            {
                size_t p = 0;
                while (p < s.exe.size())
                {
                    if (s.exe.size() - p < 11)
                        return false;
                    const auto *data = s.exe.data() + p;
                    auto *target = !memcmp(data, "KEY", 3) ? &key : !memcmp(data, "Z80", 3) ? &rom
                                                                : !memcmp(data, "SMP", 3)   ? &samples
                                                                                            : nullptr;
                    size_t n = replay_le32(data + 7);
                    if (!target || n > s.exe.size() - p - 11 || !xsf_map(*target, data + 3, n + 8, target == &key ? 11 : 32 * 1024 * 1024))
                        return false;
                    p += 11 + n;
                }
            }
            if (rom.empty() || samples.empty())
                return false;
            state.resize(qsound_get_state_size());
            qsound_clear_state(state.data());
            if (key.size() == 11)
                qsound_set_kabuki_key(state.data(), replay_le32(key.data()), replay_le32(key.data() + 4), uint16_t(key[8] | key[9] << 8), key[10]);
            else
                qsound_set_kabuki_key(state.data(), 0, 0, 0, 0);
            qsound_set_z80_rom(state.data(), rom.data(), uint32_t(rom.size()));
            qsound_set_sample_rom(state.data(), samples.data(), uint32_t(samples.size()));
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            std::array<int16_t, 1024> pcm{};
            uint32_t n = 512;
            if (qsound_execute(state.data(), 0x7fffffff, pcm.data(), &n) < 0 || !n)
                return false;
            replay_pcm16(out, pcm.data(), n);
            return true;
        }
    };
}
std::unique_ptr<replay_engine> make_qsf_engine() { return std::make_unique<qsf_engine>(); }
