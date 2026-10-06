#include "replay_engine.h"
#include "replays/AtariAudio/AtariAudioRenderer.h"

namespace
{
    struct atari_engine final : replay_engine
    {
        std::unique_ptr<AtariAudioRenderer, decltype(&AtariAudioRenderer::Destroy)>
            renderer{nullptr, AtariAudioRenderer::Destroy};
        bool load(const char *path) override
        {
            return path && load_memory(path, read_audio_file(path));
        }
        bool load_memory(const std::string &, const std::vector<uint8_t> &bytes) override
        {
            renderer.reset();
            if (bytes.size() < 16 || bytes.size() > 16 * 1024 * 1024)
                return false;
            renderer.reset(AtariAudioRenderer::Create(bytes.data(), uint32_t(bytes.size()), rate));
            if (!renderer)
                return false;
            const auto &info = renderer->GetSongInfo();
            title = info.musicName;
            artist = info.musicAuthor;
            tracks = unsigned(info.subsongCount);
            track = unsigned(info.defaultSubsong - 1);
            return tracks && track < tracks;
        }
        bool reset(unsigned i) override
        {
            if (!renderer || i >= tracks || !renderer->InitSubSong(int(i + 1)))
                return false;
            const uint32_t samples = renderer->GetSubsongDurationSample(int(i + 1));
            duration = samples ? unsigned(uint64_t(samples) * 1000 / rate) : 180000;
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            std::array<int16_t, 1024 * 2> pcm{};
            renderer->AudioRenderStereo(pcm.data(), 1024, nullptr);
            replay_pcm16(out, pcm.data(), 1024);
            return true;
        }
    };
}

auddecode *create_atari() { return new replay_decoder(std::make_unique<atari_engine>(), {"sndh", "ym"}); }
