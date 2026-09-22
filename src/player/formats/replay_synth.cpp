#include "replay_engine.h"
#include "replays/Organya/OrganyaDecoder.h"
#include "replays/V2/V2/sounddef.h"
#include "replays/V2/V2/v2mconv.h"
#include "replays/V2/V2/v2mplayer.h"
#include "replays/klystrack/klystron/src/lib/ksnd.h"
#include <mutex>

namespace
{
    struct org_engine final : replay_engine
    {
        std::unique_ptr<Organya::Song, decltype(&Organya::Unload)> song{nullptr, Organya::Unload};
        bool load(const char *path) override
        {
            static std::once_flag initialized;
            std::call_once(initialized, Organya::initialize);
            auto bytes = read_game_music(path);
            if (bytes.size() < 114)
                return false;
            song.reset(Organya::Load(bytes.data(), bytes.size(), rate));
            if (!song)
                return false;
            duration = Organya::GetDuration(song.get());
            return true;
        }
        bool reset(unsigned) override
        {
            Organya::Reset(song.get());
            return bool(song);
        }
        bool render(std::vector<float> &out) override
        {
            out = Organya::Render(song.get());
            if (out.empty())
                out = Organya::Render(song.get());
            return !out.empty();
        }
    };
    struct v2_engine final : replay_engine
    {
        std::unique_ptr<uint8_t[]> tune;
        std::unique_ptr<V2MPlayer> player;
        ~v2_engine() override
        {
            if (player)
                player->Close();
        }
        bool load(const char *path) override
        {
            if (player)
            {
                player->Close();
                player.reset();
            }
            auto bytes = read_game_music(path);
            if (bytes.size() < 12 || bytes.size() > INT32_MAX || !replay_le32(bytes.data()) || bytes[2] || bytes[3])
                return false;
            uint8_t *converted = nullptr;
            int size = 0;
            sdInit();
            ConvertV2M(bytes.data(), int(bytes.size()), &converted, &size);
            sdClose();
            tune.reset(converted);
            if (!tune || size <= 0)
                return false;
            player = std::make_unique<V2MPlayer>(false);
            if (!player->Open(tune.get(), rate))
                return false;
            int32_t *raw = nullptr;
            const auto count = player->CalcPositions(&raw);
            std::unique_ptr<int32_t[]> positions(raw);
            if (count <= 0 || !raw || raw[2 * (count - 1)] < 0)
                return false;
            duration = unsigned(raw[2 * (count - 1)]) + 2000;
            return true;
        }
        bool reset(unsigned) override
        {
            if (!player)
                return false;
            player->Play();
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            if (!player->IsPlaying())
                player->Play();
            out.resize(1024);
            player->Render(out.data(), 512);
            return true;
        }
    };
    struct klys_engine final : replay_engine
    {
        std::unique_ptr<KPlayer, decltype(&KSND_FreePlayer)> player{nullptr, KSND_FreePlayer};
        std::unique_ptr<KSong, decltype(&KSND_FreeSong)> song{nullptr, KSND_FreeSong};
        bool load(const char *path) override
        {
            song.reset();
            player.reset(KSND_CreatePlayerUnregistered(rate));
            auto bytes = read_game_music(path);
            if (!player || bytes.size() < 16 || bytes.size() > INT32_MAX)
                return false;
            song.reset(KSND_LoadSongFromMemory(player.get(), bytes.data(), int(bytes.size())));
            if (!song)
                return false;
            KSongInfo info{};
            KSND_GetSongInfo(song.get(), &info);
            if (info.song_title)
                title = info.song_title;
            KSND_SetPlayerQuality(player.get(), 4);
            int length = KSND_GetPlayTime(song.get(), KSND_GetSongLength(song.get()));
            if (length <= 0)
                return false;
            duration = unsigned(length);
            return true;
        }
        bool reset(unsigned) override
        {
            if (!song)
                return false;
            KSND_PlaySong(player.get(), song.get(), 0);
            KSND_SetLooping(player.get(), 1);
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            std::array<int16_t, 1024> pcm{};
            int count = KSND_FillBuffer(player.get(), pcm.data(), sizeof(pcm));
            if (!count)
                count = KSND_FillBuffer(player.get(), pcm.data(), sizeof(pcm));
            if (count <= 0 || count > 512)
                return false;
            replay_pcm16(out, pcm.data(), size_t(count));
            return true;
        }
    };
}
auddecode *create_organya() { return new replay_decoder(std::make_unique<org_engine>(), {"org"}); }
auddecode *create_v2m() { return new replay_decoder(std::make_unique<v2_engine>(), {"v2m"}); }
auddecode *create_klystrack() { return new replay_decoder(std::make_unique<klys_engine>(), {"kt"}); }
