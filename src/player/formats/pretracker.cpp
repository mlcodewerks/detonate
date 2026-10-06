#include "../replayer_settings.h"
#include "replay_engine.h"
extern "C" {
#include "replays/PreTracker/pretracker/pretracker.h"
}

namespace
{
    struct pretracker_engine final : replay_engine
    {
        std::unique_ptr<PreSong, decltype(&pre_song_destroy)> song{nullptr, pre_song_destroy};
        std::vector<uint8_t> data;
        std::vector<unsigned> lengths;
        bool load(const char *p) override
        {
            std::error_code ec;
            auto size = std::filesystem::file_size(std::filesystem::path(reinterpret_cast<const char8_t *>(p)),ec);
            return !ec && size <= 1024 * 1024 && load_memory(p,read_audio_file(p));
        }
        bool load_memory(const std::string &, const std::vector<uint8_t> &bytes) override
        {
            song.reset(); lengths.clear();
            if (bytes.size() < 0x42 || bytes.size() > 1024 * 1024 || std::memcmp(bytes.data(), "PRT", 3) ||
                (bytes[3] > 0x1b && bytes[3] != 0x1e) || (bytes[3] == 0x1e && bytes.size() < 0x5b)) return false;
            data = bytes;
            song.reset(pre_song_create(data.data(), uint32_t(data.size())));
            if (!song) return false;
            auto *meta = pre_song_get_metadata(song.get());
            // The native sequencer always dereferences wave zero, even on silent channels.
            if (!meta->num_waves) { song.reset(); return false; }
            title = meta->song_name; artist = meta->author;
            tracks = std::max(1u, unsigned(meta->num_subsongs)); track = 0;
            lengths.resize(tracks);
            return reset(0);
        }
        bool reset(unsigned i) override
        {
            if (!song || i >= tracks) return false;
            const auto s = replayer_settings::snapshot();
            pre_song_set_interp_mode(song.get(), s[replayer_settings::pre_interpolation] ? PRE_INTERP_SINC : PRE_INTERP_BLEP);
            pre_song_set_stereo_mix(song.get(), s[replayer_settings::pre_mix] / 100.f);
            pre_song_set_stereo_width(song.get(), s[replayer_settings::pre_delay] / 1000.f);
            pre_song_set_subsong(song.get(), i);
            pre_song_set_sample_rate(song.get(), rate);
            pre_song_start(song.get());
            if (!lengths[i])
            {
                std::array<float, 2048> pcm;
                uint64_t frames = 0;
                while (!pre_song_is_finished(song.get()) && frames < uint64_t(rate) * 600)
                {
                    auto n = pre_song_decode(song.get(), pcm.data(), 1024);
                    if (n <= 0) break;
                    frames += n;
                }
                lengths[i] = std::max(1u, unsigned(frames * 1000 / rate));
                pre_song_start(song.get());
            }
            duration = lengths[i]; track = i;
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            out.resize(2048);
            int n = pre_song_decode(song.get(), out.data(), 1024);
            if (n <= 0) return false;
            out.resize(size_t(n) * 2);
            return true;
        }
    };
}
auddecode *create_pretracker()
{
    return new replay_decoder(std::make_unique<pretracker_engine>(), {"prt"});
}
