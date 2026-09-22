#include "xsf_loader.h"
bool Snes9xInit(const uint8_t *, size_t, const uint8_t *, size_t);
void Snes9xRelease();
bool Snes9xRender(int16_t *, uint32_t);
namespace
{
    struct snsf_engine final : xsf_engine
    {
        std::vector<uint8_t> rom, sram;
        bool initialized = false;
        snsf_engine() { rate = 48000; }
        ~snsf_engine() override
        {
            if (initialized)
                Snes9xRelease();
        }
        bool accepts(uint8_t v) const override { return v == 0x23; }
        bool reset(unsigned) override
        {
            if (initialized)
            {
                Snes9xRelease();
                initialized = false;
            }
            rom.clear();
            sram.clear();
            for (auto &s : file.sections)
            {
                if (!s.exe.empty() && !xsf_map(rom, s.exe.data(), s.exe.size(), 16 * 1024 * 1024, true))
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
                        if (!xsf_map(sram, data.data(), data.size(), 512 * 1024))
                            return false;
                    }
                    p += 12 + n;
                }
            }
            if (rom.empty())
                return false;
            initialized = Snes9xInit(rom.data(), rom.size(), sram.data(), sram.size());
            return initialized;
        }
        bool render(std::vector<float> &out) override
        {
            std::array<int16_t, 1024> pcm{};
            if (!initialized || !Snes9xRender(pcm.data(), 512))
                return false;
            replay_pcm16(out, pcm.data(), 512);
            return true;
        }
    };
}
std::unique_ptr<replay_engine> make_snsf_engine() { return std::make_unique<snsf_engine>(); }
