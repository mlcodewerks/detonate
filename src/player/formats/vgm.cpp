#include "game_music_io.h"
#include "vgm_validate.h"
#include "libvgm/player/playera.hpp"
#include "libvgm/player/vgmplayer.hpp"
#include "libvgm/utils/MemoryLoader.h"
#include <cstring>
#include <memory>

class vgm_decoder final : public decoder_base
{
    std::vector<uint8_t> bytes_;
    // Player must release all references before the loader and bytes are freed.
    std::unique_ptr<DATA_LOADER, decltype(&DataLoader_Deinit)> loader_{nullptr, DataLoader_Deinit};
    std::unique_ptr<PlayerA> player_;
    std::vector<int16_t> pcm_;
    std::string title_;
    std::filesystem::path directory_;
    std::vector<uint8_t> rom_;
    bool missing_rom_ = false;

    static DATA_LOADER *load_rom(void *context, PlayerBase *, const char *name)
    {
        auto &self = *static_cast<vgm_decoder *>(context);
        if (std::strcmp(name, "yrw801.rom"))
            return nullptr;
        try
        {
            const auto path = (self.directory_ / "yrw801.rom").u8string();
            self.rom_ = read_game_music(reinterpret_cast<const char *>(path.c_str()));
            std::unique_ptr<DATA_LOADER, decltype(&DataLoader_Deinit)> loader(
                MemoryLoader_Init(self.rom_.data(), static_cast<UINT32>(self.rom_.size())), DataLoader_Deinit);
            if (loader && !DataLoader_Load(loader.get()) && !self.rom_.empty())
                return loader.release();
        }
        catch (const std::exception &)
        {
        }
        self.missing_rom_ = true;
        return nullptr;
    }

    size_t read_frames(float *out, size_t frames) override
    {
        if (!loop_ && length_)
        {
            if (position_ >= length_)
                return 0;
            frames = std::min<uint64_t>(frames, length_ - position_);
        }
        if (player_->GetState() & PLAYSTATE_FIN)
            return 0;
        frames = std::min<size_t>(frames, 4096);
        pcm_.resize(frames * 2);
        const size_t got = player_->Render(static_cast<UINT32>(frames * 4), pcm_.data()) / 4;
        for (size_t i = 0; i < got * 2; ++i)
            out[i] = pcm_[i] / 32768.0f;
        return got;
    }
    bool seek_frame(uint64_t frame) override
    {
        if (!player_ || frame > UINT32_MAX)
            return false;
        if (!frame)
            return player_->Reset() == 0;
        return player_->Seek(PLAYPOS_SAMPLE, static_cast<UINT32>(frame)) == 0;
    }

public:
    ~vgm_decoder() override { stop(); }
    bool supports_looping() const override { return true; }
    void set_loop(bool loop) override
    {
        decoder_base::set_loop(loop);
        if (player_)
        {
            player_->SetLoopCount(0); 
            player_->SetFadeSamples(0);
            player_->SetEndSilenceSamples(0);
        }
    }
    std::vector<std::string> file_types() override { return {"vgm", "vgz"}; }
    const char *song_title() override { return title_.c_str(); }
    bool open(const char *filename, float *rate, bool loop) override
    {
        stop();
        directory_ = std::filesystem::path(reinterpret_cast<const char8_t *>(filename)).parent_path();
        bytes_ = read_game_music(filename);
        if (!valid_vgm(bytes_))
            return false;
        loader_.reset(MemoryLoader_Init(bytes_.data(), static_cast<UINT32>(bytes_.size())));
        if (!loader_ || DataLoader_Load(loader_.get()))
        {
            stop();
            return false;
        }
        player_ = std::make_unique<PlayerA>();
        player_->RegisterPlayerEngine(new VGMPlayer);
        player_->SetFileReqCallback(load_rom, this);
        if (player_->SetOutputSettings(44100, 2, 16, 4096) || player_->LoadFile(loader_.get()))
        {
            stop();
            return false;
        }
        player_->SetLoopCount(0); 
        player_->SetFadeSamples(0);
        player_->SetEndSilenceSamples(0);
        rate_ = 44100;
        channels_ = 2;
        loop_ = loop;
        const auto *engine = player_->GetPlayer();
        length_ = engine->Tick2Sample(engine->GetTotalTicks());
        const auto tags = player_->GetPlayer()->GetTags();
        if (tags)
            for (size_t i = 0; tags[i] && tags[i + 1]; i += 2)
            {
                if (!std::strcmp(tags[i], "TITLE"))
                    title_ = tags[i + 1];
                else if (!std::strcmp(tags[i], "TITLE-JPN") && title_.empty())
                    title_ = tags[i + 1];
                else if (!std::strcmp(tags[i], "ARTIST"))
                    metadata_.artist = tags[i + 1];
                else if (!std::strcmp(tags[i], "ARTIST-JPN") && metadata_.artist.empty())
                    metadata_.artist = tags[i + 1];
                else if (!std::strcmp(tags[i], "GAME"))
                    metadata_.album = tags[i + 1];
                else if (!std::strcmp(tags[i], "GAME-JPN") && metadata_.album.empty())
                    metadata_.album = tags[i + 1];
            }
        if (player_->Start())
        {
            stop();
            return false;
        }
        if (missing_rom_)
            throw std::runtime_error("This OPL4 VGM requires yrw801.rom beside the music file.");
        position_ = 0;
        playing_ = true;
        *rate = float(rate_);
        return true;
    }
    void stop() override
    {
        player_.reset();
        loader_.reset();
        bytes_.clear();
        pcm_.clear();
        title_.clear();
        metadata_.clear();
        rom_.clear();
        directory_.clear();
        missing_rom_ = false;
        playing_ = loop_ = false;
        rate_ = channels_ = 0;
        position_ = length_ = 0;
    }
};

auddecode *create_vgm() { return new vgm_decoder; }
