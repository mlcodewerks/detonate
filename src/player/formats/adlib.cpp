#include "../replayer_settings.h"
#include "replay_engine.h"
#include "replays/AdLib/adplug/adplug.h"
#include "replays/AdLib/adplug/nemuopl.h"
#include "replays/AdLib/adplug/surroundopl.h"
#include "replays/AdLib/adplug/libbinio/binstr.h"
#include <cmath>

namespace
{
    struct memory_provider final : CFileProvider
    {
        std::string name;
        const std::vector<uint8_t> &bytes;
        bool disk;
        memory_provider(std::string n, const std::vector<uint8_t> &b, bool d) : name(std::move(n)), bytes(b), disk(d) {}
        binistream *open(std::string filename) const override
        {
            if (filename == name)
                return new binisstream(const_cast<uint8_t *>(bytes.data()), bytes.size());
            if (!disk)
                return nullptr;
            std::error_code ec;
            auto size = std::filesystem::file_size(std::filesystem::path(reinterpret_cast<const char8_t *>(filename.c_str())),ec);
            if (ec || size > 16 * 1024 * 1024) return nullptr;
            auto data = read_audio_file(filename.c_str());
            if (data.empty() || data.size() > 16 * 1024 * 1024)
                return nullptr;
            // Companion bytes must outlive the stream returned to the loader.
            struct owned_stream : binisstream
            {
                std::vector<uint8_t> storage;
                owned_stream(std::vector<uint8_t> b) : binsbase(nullptr, 0), binisstream(nullptr, 0), storage(std::move(b))
                { data = storage.data(); spos = data; length = storage.size(); }
            };
            return new owned_stream(std::move(data));
        }
        void close(binistream *s) const override { delete s; }
    };
    struct adlib_engine final : replay_engine
    {
        std::unique_ptr<Copl> opl;
        bool surround = false;
        std::unique_ptr<CPlayer> player;
        std::vector<uint8_t> data;
        std::string name;
        bool disk = false;
        double fraction = 0;
        bool create_player()
        {
            player.reset(); opl.reset();
            surround = replayer_settings::snapshot()[replayer_settings::adlib_surround] != 0;
            auto a = std::make_unique<CNemuopl>(rate);
            if (surround)
            {
                auto b = std::make_unique<CNemuopl>(rate);
                COPLprops left{a.get(), true, true}, right{b.get(), true, true};
                opl = std::make_unique<CSurroundopl>(&left, &right, true);
                a.release(); b.release();
            }
            else opl = std::move(a);
            opl->init();
            memory_provider fp(name, data, disk);
            player.reset(CAdPlug::factory(name, opl.get(), CAdPlug::players, fp));
            return bool(player);
        }
        bool initialize(const std::string &n, const std::vector<uint8_t> &b, bool d)
        {
            player.reset(); opl.reset();
            if (b.size() < 8 || b.size() > 1024 * 1024) return false;
            name = n; data = b; disk = d;
            if (!create_player()) return false;
            title = player->gettitle(); artist = player->getauthor();
            tracks = std::max(1u, player->getsubsongs()); track = 0;
            return reset(0);
        }
        bool load(const char *p) override
        {
            std::error_code ec;
            auto size = std::filesystem::file_size(std::filesystem::path(reinterpret_cast<const char8_t *>(p)),ec);
            return !ec && size <= 1024 * 1024 && initialize(p,read_audio_file(p),true);
        }
        bool load_memory(const std::string &n, const std::vector<uint8_t> &b) override { return initialize(n, b, false); }
        bool reset(unsigned i) override
        {
            if (!player || i >= tracks) return false;
            if (surround != (replayer_settings::snapshot()[replayer_settings::adlib_surround] != 0) && !create_player()) return false;
            duration = std::max(1ul, player->songlength(i));
            opl->init(); player->rewind(i); fraction = 0; track = i;
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            player->update(); // The native replayers continue through their loop marker.
            double refresh = player->getrefresh();
            if (!std::isfinite(refresh) || refresh < 1 || refresh > rate) return false;
            fraction += rate / refresh;
            unsigned frames = unsigned(fraction); fraction -= frames;
            std::vector<int16_t> pcm(frames * 2);
            // Bound each call because the harmonic wrapper uses a short buffer size.
            for (unsigned offset = 0; offset < frames; offset += 1024)
                opl->update(pcm.data() + offset * 2, std::min(1024u, frames - offset));
            replay_pcm16(out, pcm.data(), frames);
            return true;
        }
    };
}
auddecode *create_adlib()
{
    std::vector<std::string> types;
    for (auto *desc : CAdPlug::players)
        for (unsigned i = 0; desc->get_extension(i); ++i)
            types.emplace_back(desc->get_extension(i) + 1);
    return new replay_decoder(std::make_unique<adlib_engine>(), std::move(types));
}
