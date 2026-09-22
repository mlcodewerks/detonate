#include "replay_engine.h"
#include "replays/SidPlay/libsidplayfp/sidplayfp/sidplayfp.h"
#include "replays/SidPlay/libsidplayfp/sidplayfp/SidTune.h"
#include "replays/SidPlay/libsidplayfp/sidplayfp/SidTuneInfo.h"
#include "replays/SidPlay/libsidplayfp/builders/residfp-builder/residfp.h"

namespace
{
    struct sid_engine final : replay_engine
    {
        std::unique_ptr<SidTune> tune;
        std::unique_ptr<ReSIDfpBuilder> builder;
        std::unique_ptr<sidplayfp> player;
        std::vector<uint8_t> kernal, basic, character;
        bool load(const char *path) override
        {
            player.reset();
            builder.reset();
            tune.reset();
            auto bytes = read_game_music(path);
            if (bytes.size() < 118 || (std::memcmp(bytes.data(), "PSID", 4) && std::memcmp(bytes.data(), "RSID", 4)))
                return false;
            tune = std::make_unique<SidTune>(bytes.data(), uint32_t(bytes.size()));
            if (!tune->getStatus())
                return false;
            const auto *info = tune->getInfo();
            tracks = info->songs();
            track = info->startSong() ? info->startSong() - 1 : 0;
            if (!tracks)
                return false;
            if (info->numberOfInfoStrings())
                title = info->infoString(0);
            if (info->numberOfInfoStrings() > 1)
                artist = info->infoString(1);
            builder = std::make_unique<ReSIDfpBuilder>("Detonate");
            player = std::make_unique<sidplayfp>();
            auto dir = std::filesystem::path(reinterpret_cast<const char8_t *>(path)).parent_path();
            auto rom = [&dir](const char *name, size_t size)
            {
                auto path = (dir / name).u8string();
                auto bytes = read_audio_file(reinterpret_cast<const char *>(path.c_str()));
                if (bytes.size() != size)
                    bytes.clear();
                return bytes;
            };
            kernal = rom("kernal", 8192);
            basic = rom("basic", 8192);
            character = rom("chargen", 4096);
            if (!kernal.empty())
                player->setRoms(kernal.data(), basic.empty() ? nullptr : basic.data(), character.empty() ? nullptr : character.data());
            SidConfig cfg;
            cfg.frequency = rate;
            cfg.sidEmulation = builder.get();
            cfg.powerOnDelay = 0;
            cfg.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;
            if (!player->config(cfg))
                throw std::runtime_error(player->error());
            return true;
        }
        bool reset(unsigned i) override
        {
            if (!tune || i >= tracks)
                return false;
            tune->selectSong(i + 1);
            if (!player->load(tune.get()))
                throw std::runtime_error(player->error());
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            int n = player->play(10000);
            if (n <= 0)
                return false;
            std::array<SampleI16 *, 3> buffers{};
            player->buffers(buffers.data());
            unsigned chips = player->installedSIDs();
            if (!chips || chips > buffers.size())
                return false;
            out.assign(size_t(n) * 2, 0);
            for (unsigned c = 0; c < chips; ++c)
                for (int i = 0; i < n; ++i)
                {
                    out[2 * i] += buffers[c][i].left / (32768.f * chips);
                    out[2 * i + 1] += buffers[c][i].right / (32768.f * chips);
                }
            return true;
        }
    };
}
auddecode *create_sid() { return new replay_decoder(std::make_unique<sid_engine>(), {"sid"}); }
