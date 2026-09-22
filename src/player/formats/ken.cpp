#include "replay_engine.h"
#include "replays/Ken/backend.h"

namespace
{
    struct ken_engine final : replay_engine
    {
        std::unique_ptr<ken::player> player;
        std::vector<uint8_t> data, waves;
        std::string ext;
        bool initialize(const std::string &name, const std::vector<uint8_t> &bytes, bool disk)
        {
            player.reset();
            data.clear();
            waves.clear();
            if (bytes.empty() || bytes.size() > 1024 * 1024)
                return false;
            ext = std::filesystem::path(reinterpret_cast<const char8_t *>(name.c_str())).extension().string();
            if (!ext.empty())
                ext.erase(0, 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                           { return char(std::tolower(c)); });
            if (ext != "kdm" && ext != "ksm" && ext != "sm" && ext != "snd")
                return false;
            audiotags::Metadata tags;
            audiotags::read_memory(bytes.data(), bytes.size(), tags);
            title = tags.title;
            artist = tags.artist;
            album = tags.album;
            data = bytes;
            if (data.size() >= 128 && !std::memcmp(data.data() + data.size() - 128, "TAG", 3))
                data.resize(data.size() - 128);
            if (data.size() >= 32 && !std::memcmp(data.data() + data.size() - 32, "APETAGEX", 8))
            {
                const auto size = replay_le32(data.data() + data.size() - 20);
                if (size >= 32 && size <= data.size())
                {
                    data.resize(data.size() - size);
                    if (data.size() >= 32 && !std::memcmp(data.data() + data.size() - 32, "APETAGEX", 8))
                        data.resize(data.size() - 32);
                }
            }
            if (ext == "kdm")
            {
                if (!disk)
                    return false; 
                auto path = std::filesystem::path(reinterpret_cast<const char8_t *>(name.c_str())).parent_path() / "waves.kwv";
                std::error_code ec;
                if (!std::filesystem::exists(path, ec))
                    path.replace_filename("WAVES.KWV");
                auto size = std::filesystem::file_size(path, ec);
                if (ec || size > 16 * 1024 * 1024)
                    return false;
                const auto u8 = path.u8string();
                waves = read_audio_file(reinterpret_cast<const char *>(u8.c_str()));
            }
            tracks = 1;
            track = 0;
            return reset(0);
        }
        bool load(const char *path) override
        {
            std::error_code ec;
            auto size = std::filesystem::file_size(std::filesystem::path(reinterpret_cast<const char8_t *>(path)), ec);
            return !ec && size <= 1024 * 1024 && initialize(path, read_audio_file(path), true);
        }
        bool load_memory(const std::string &name, const std::vector<uint8_t> &bytes) override { return initialize(name, bytes, false); }
        bool reset(unsigned song) override
        {
            if (song)
                return false;
            player = ken::load(ext, data, waves, rate);
            if (!player)
                return false;
            duration = player->duration();
            return duration > 0;
        }
        bool render(std::vector<float> &out) override
        {
            const unsigned frames = ext == "kdm" ? rate / 120 : 512;
            std::array<int16_t, 1024> pcm{};
            player->render(pcm.data(), frames);
            replay_pcm16(out, pcm.data(), frames);
            return true;
        }
    };
}
auddecode *create_ken()
{
    return new replay_decoder(std::make_unique<ken_engine>(), {"kdm", "ksm", "sm", "snd"});
}
