#include "replay_engine.h"
#include <mutex>
extern "C"
{
#include "replays/Hively/hvl/hvl_replay.h"
}
namespace
{
    bool valid_hively(const std::vector<uint8_t> &b)
    {
        if (b.size() < 16)
            return false;
        bool hvl = !std::memcmp(b.data(), "HVL", 3) && b[3] < 2;
        if (!hvl && (std::memcmp(b.data(), "THX", 3) || b[3] > 2))
            return false;
        size_t positions = ((b[6] & 15) << 8) | b[7], channels = hvl ? (b[8] >> 2) + 4 : 4;
        if (!positions || positions > 1000 || channels > MAX_CHANNELS || !b[10] || b[10] > 64 || b[12] > 64 || (hvl && b[15] > 4))
            return false;
        size_t pos = (hvl ? 16 : 14) + b[13] * 2;
        auto take = [&](size_t n)
        { if (pos > b.size() || n > b.size() - pos) return false; pos += n; return true; };
        if (!take(positions * channels * 2))
            return false;
        for (unsigned i = (b[6] & 128) ? 1 : 0; i <= b[11]; ++i)
            for (unsigned j = 0; j < b[10]; ++j)
            {
                if (pos >= b.size() || !take(hvl ? (b[pos] == 63 ? 1 : 5) : 3))
                    return false;
            }
        for (unsigned i = 0; i < b[12]; ++i)
        {
            if (pos > b.size() || b.size() - pos < 22 || (b[pos + 1] & 7) > 5)
                return false;
            if (!take(22 + b[pos + 21] * (hvl ? 5 : 4)))
                return false;
        }
        size_t name = (b[4] << 8) | b[5];
        for (unsigned i = 0; i <= b[12]; ++i)
        {
            if (name >= b.size())
                return i > 0; // instrument names are optional
            const auto *end = static_cast<const uint8_t *>(std::memchr(b.data() + name, 0, b.size() - name));
            if (!end)
                return false;
            name = size_t(end - b.data()) + 1;
        }
        return true;
    }
    struct hively_engine final : replay_engine
    {
        std::unique_ptr<hvl_tune, decltype(&hvl_FreeTune)> tune{nullptr, hvl_FreeTune};
        std::vector<unsigned> durations;
        bool load(const char *path) override
        {
            auto bytes = read_game_music(path);
            if (!valid_hively(bytes))
                return false;
            static std::once_flag once;
            std::call_once(once, hvl_InitReplayer);
            tune.reset(hvl_ParseTune(bytes.data(), uint32_t(bytes.size()), rate, 2, [](size_t size) -> void *
                                     { return std::calloc(1, size); }, std::free));
            if (!tune)
                return false;
            title = tune->ht_Name;
            tracks = tune->ht_SubsongNr + 1;
            track = 0;
            durations.clear();
            for (unsigned i = 0; i < tracks; ++i)
            {
                if (!hvl_InitSubsong(tune.get(), i))
                    return false;
                const unsigned hz = 50 * tune->ht_SpeedMultiplier;
                unsigned ticks = 0;
                do
                {
                    hvl_play_irq(tune.get());
                    ++ticks;
                } while (!tune->ht_SongEndReached && ticks < hz * 3600);
                if (!tune->ht_SongEndReached)
                    return false;
                durations.push_back(unsigned(uint64_t(ticks) * 1000 / hz));
            }
            return true;
        }
        bool reset(unsigned i) override
        {
            if (!tune || i >= durations.size())
                return false;
            duration = durations[i];
            return hvl_InitSubsong(tune.get(), i);
        }
        bool render(std::vector<float> &out) override
        {
            const unsigned count = rate / 50 / tune->ht_SpeedMultiplier;
            std::vector<int16_t> pcm(count * 2);
            hvl_Decode(tune.get(), pcm.data(), count);
            replay_pcm16(out, pcm.data(), count);
            return true;
        }
    };
}
auddecode *create_hively() { return new replay_decoder(std::make_unique<hively_engine>(), {"hvl", "ahx"}); }
