#include "replay_engine.h"
#include "replays/FutureComposer/libtfmxaudiodecoder/src/DecoderProxy.h"
#include "futurecomposer_format.h"

namespace
{
    constexpr size_t module_limit = 1024 * 1024, sample_limit = 16 * 1024 * 1024;
    struct futurecomposer_engine final : replay_engine
    {
        tfmxaudiodecoder::LamePaulaMixer mixer;
        std::unique_ptr<tfmxaudiodecoder::DecoderProxy> decoder;
        audiotags::Metadata tags;
        bool disk_companions = false;

        static std::pair<uint8_t *, uint32_t> companion(void *user, const char *path)
        {
            auto &self = *static_cast<futurecomposer_engine *>(user);
            if (!self.disk_companions || !path)
                return {};
            try
            {
                const auto file = std::filesystem::path(reinterpret_cast<const char8_t *>(path));
                std::error_code ec;
                if (std::filesystem::file_size(file, ec) > sample_limit || ec)
                    return {};
                auto bytes = read_audio_file(path);
                if (bytes.empty() || bytes.size() > sample_limit)
                    return {};
                auto result = std::make_unique<uint8_t[]>(bytes.size());
                std::copy(bytes.begin(), bytes.end(), result.get());
                return {result.release(), uint32_t(bytes.size())};
            }
            catch (const std::exception &)
            {
                return {};
            }
        }
        void metadata()
        {
            const auto native = [&](const char *key)
            {
                const char *value = decoder->getInfoString(key);
                return value ? std::string(value) : std::string{};
            };
            title = tags.title.empty() ? native("title") : tags.title;
            artist = tags.artist.empty() ? native("artist") : tags.artist;
            album = tags.album.empty() ? native("game") : tags.album;
        }
        bool initialize(const std::string &name, const std::vector<uint8_t> &bytes)
        {
            decoder.reset();
            tags.clear();
            if (bytes.size() < 5 || bytes.size() > module_limit)
                return false;
            if (futurecomposer::signature(bytes.data(), bytes.size()) &&
                !futurecomposer::valid(bytes.data(), bytes.size()))
                return false;
            decoder = std::make_unique<tfmxaudiodecoder::DecoderProxy>();
            decoder->setPath(name.c_str(), companion, this);
            mixer.init(rate, 16, 2, 0, 100);
            decoder->setMixer(&mixer);
            if (!decoder->init(const_cast<uint8_t *>(bytes.data()), uint32_t(bytes.size()), 0))
            {
                decoder.reset();
                return false;
            }
            const int count = decoder->getSongs();
            if (count <= 0 || count > 256)
            {
                decoder.reset();
                return false;
            }
            tracks = unsigned(count);
            track = 0;
            audiotags::read_memory(bytes.data(), bytes.size(), tags);
            tags.pictures.clear();
            metadata();
            return true;
        }
        bool load(const char *path) override
        {
            disk_companions = true;
            std::error_code ec;
            const auto size = std::filesystem::file_size(std::filesystem::path(reinterpret_cast<const char8_t *>(path)), ec);
            if (ec || size > module_limit)
                return false;
            return initialize(path, read_audio_file(path));
        }
        bool load_memory(const std::string &name, const std::vector<uint8_t> &bytes) override
        {
            disk_companions = false;
            return initialize(name, bytes);
        }
        bool reset(unsigned song) override
        {
            if (!decoder || song >= tracks)
                return false;
            mixer.init(rate, 16, 2, 0, 100);
            if (!decoder->reinit(int(song)))
                return false;
            duration = decoder->getDuration();
            if (!duration || duration >= 59 * 60 * 1000)
                return false;
            decoder->setLoopMode(1); // The shared adapter bounds non-repeat playback.
            metadata();
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            std::array<int16_t, 1024> pcm;
            auto bytes = decoder->mixerFillBuffer(pcm.data(), sizeof(pcm));
            if (!bytes)
                bytes = decoder->mixerFillBuffer(pcm.data(), sizeof(pcm));
            if (!bytes || bytes > sizeof(pcm) || bytes % 4)
                return false;
            replay_pcm16(out, pcm.data(), bytes / 4);
            return true;
        }
    };
}
auddecode *create_futurecomposer()
{
    return new replay_decoder(std::make_unique<futurecomposer_engine>(),
                              {"fc", "fc13", "fc14", "fc3", "fc4", "smod", "hip", "hip7", "hipc", "mcmd", "tfmx", "tfx", "tfm", "mdat", "dns"});
}
