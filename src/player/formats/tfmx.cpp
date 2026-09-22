#include "replay_engine.h"
extern "C"
{
#include "replays/TFMX/TFMX/tfmx.h"
#include "replays/TFMX/TFMX/tfmx_iface.h"
}
namespace
{
    struct tfmx_delete
    {
        void operator()(TfmxState *s) const
        {
            if (s)
            {
                TFMXQuit(s);
                delete s;
            }
        }
    };
    struct tfmx_engine final : replay_engine
    {
        std::unique_ptr<TfmxState, tfmx_delete> state;
        std::vector<unsigned> durations;
        bool load(const char *path) override
        {
            auto bytes = read_game_music(path);
            if (bytes.size() < 512 || bytes.size() > 16 * 1024 * 1024)
                return false;
            std::vector<uint8_t> samples;
            TfmxData mdat{}, smpl{};
            if (!std::memcmp(bytes.data(), "TFMX-MOD", 8))
            {
                const size_t offset = replay_le32(bytes.data() + 8), end = replay_le32(bytes.data() + 12);
                if (offset < 532 || offset > end || end > bytes.size())
                    return false;
                mdat = {bytes.data() + 20, offset - 20, 0};
                smpl = {bytes.data() + offset, end - offset, 0};
            }
            else
            {
                if (std::memcmp(bytes.data(), "TFMX", 4))
                    return false;
                auto file = std::filesystem::path(reinterpret_cast<const char8_t *>(path));
                auto companion = file;
                companion.replace_extension("smpl");
                auto name = file.filename().u8string();
                if (name.size() > 5 && (name.substr(0, 5) == u8"mdat." || name.substr(0, 5) == u8"MDAT."))
                    companion = file.parent_path() / (u8"smpl." + name.substr(5));
                auto u8 = companion.u8string();
                samples = read_audio_file(reinterpret_cast<const char *>(u8.c_str()));
                if (samples.empty())
                {
                    companion = file;
                    companion.replace_extension("sam");
                    u8 = companion.u8string();
                    samples = read_audio_file(reinterpret_cast<const char *>(u8.c_str()));
                }
                if (samples.empty())
                    throw std::runtime_error("TFMX sample file (.smpl, smpl.* or .sam) is missing.");
                mdat = {bytes.data(), bytes.size(), 0};
                smpl = {samples.data(), samples.size(), 0};
            }
            if (mdat.size > 65536 || smpl.size > 16 * 1024 * 1024)
                return false;
            state.reset(new TfmxState{});
            TfmxState_init(state.get());
            state->outRate = rate;
            if (LoadTFMXFile(state.get(), &mdat, &smpl) < 0)
                return false;
            tracks = TFMXGetSubSongs(state.get());
            track = 0;
            if (!tracks || tracks > 32)
                return false;
            title.assign(state->mdat_header.text[0], 40);
            while (!title.empty() && (title.back() == ' ' || title.back() == 0))
                title.pop_back();
            durations.clear();
            for (unsigned i = 0; i < tracks; ++i)
            {
                state->loops = 1;
                TFMXSetSubSong(state.get(), i);
                uint64_t samples = 0;
                for (unsigned ticks = 0; state->mdb.PlayerEnable && ticks < 50 * 3600; ++ticks)
                {
                    player_tfmxIrqIn(state.get());
                    if (state->hasUnsuportedCommands)
                        return false;
                    samples += uint64_t(state->eClocks) * rate / 715910;
                }
                if (state->mdb.PlayerEnable)
                    return false;
                durations.push_back(unsigned(samples * 1000 / rate));
            }
            return true;
        }
        bool reset(unsigned i) override
        {
            if (!state || i >= durations.size())
                return false;
            duration = durations[i];
            state->loops = -1;
            TFMXSetSubSong(state.get(), i);
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            for (unsigned attempt = 0; attempt < 4096; ++attempt)
            {
                auto *pcm = reinterpret_cast<const float *>(tfmx_get_next_buffer(state.get()));
                if (pcm)
                {
                    out.assign(pcm, pcm + state->blocksize * 2);
                    return true;
                }
                if (tfmx_try_to_make_block(state.get()) < 0)
                    return false;
            }
            return false;
        }
    };
}
auddecode *create_tfmx() { return new replay_decoder(std::make_unique<tfmx_engine>(), {"tfm", "tfx", "mdat", "tfmx"}); }
