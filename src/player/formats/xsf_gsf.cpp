#include "xsf_loader.h"
#include <mgba/core/core.h>
#include <mgba/core/blip_buf.h>
#include <mgba/core/config.h>
#include <mgba-util/vfs.h>
namespace
{
    struct gsf_engine final : xsf_engine
    {
        mCore *core = nullptr;
        std::vector<uint8_t> rom;
        struct audio_stream : mAVStream
        {
            std::vector<int16_t> pcm;
        } stream{};
        void release()
        {
            if (core)
            {
                mCoreConfigDeinit(&core->config);
                core->deinit(core);
                core = nullptr;
            }
        }
        ~gsf_engine() override { release(); }
        gsf_engine()
        {
            rate = 48000;
            stream.postAudioBuffer = [](mAVStream *s, blip_t *left, blip_t *right)
            {
                auto *a = static_cast<audio_stream *>(s);
                int n = std::min(blip_samples_avail(left), blip_samples_avail(right));
                if (n <= 0)
                    return;
                size_t offset = a->pcm.size();
                a->pcm.resize(offset + size_t(n) * 2);
                blip_read_samples(left, a->pcm.data() + offset, n, true);
                blip_read_samples(right, a->pcm.data() + offset + 1, n, true);
            };
        }
        bool accepts(uint8_t v) const override { return v == 0x22; }
        bool reset(unsigned) override
        {
            release();
            rom.clear();
            stream.pcm.clear();
            for (auto &s : file.sections)
                if (!s.exe.empty())
                {
                    if (s.exe.size() < 12)
                        return false;
                    auto data = s.exe;
                    uint32_t offset = replay_le32(data.data() + 4) & 0x1ffffff;
                    for (unsigned i = 0; i < 4; ++i)
                        data[4 + i] = uint8_t(offset >> (8 * i));
                    if (!xsf_map(rom, data.data() + 4, data.size() - 4, 32 * 1024 * 1024, true))
                        return false;
                }
            if (rom.empty())
                return false;
            auto *vf = VFileFromConstMemory(rom.data(), rom.size());
            core = mCoreFindVF(vf);
            if (!core)
            {
                vf->close(vf);
                return false;
            }
            if (!core->init(core))
            {
                vf->close(vf);
                core = nullptr;
                return false;
            }
            mCoreInitConfig(core, nullptr);
            core->setAVStream(core, &stream);
            core->setAudioBufferSize(core, 2048);
            blip_set_rates(core->getAudioChannel(core, 0), core->frequency(core), rate);
            blip_set_rates(core->getAudioChannel(core, 1), core->frequency(core), rate);
            mCoreOptions opts{};
            opts.useBios = false;
            opts.skipBios = true;
            opts.volume = 0x100;
            opts.sampleRate = rate;
            mCoreConfigLoadDefaults(&core->config, &opts);
            if (!core->loadROM(core, vf))
            {
                vf->close(vf);
                release();
                return false;
            }
            core->reset(core);
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            if (!core)
                return false;
            stream.pcm.clear();
            for (unsigned i = 0; stream.pcm.empty() && i < 600; ++i)
                core->runFrame(core);
            if (stream.pcm.empty())
                return false;
            replay_pcm16(out, stream.pcm.data(), stream.pcm.size() / 2);
            return true;
        }
    };
}
std::unique_ptr<replay_engine> make_gsf_engine() { return std::make_unique<gsf_engine>(); }
