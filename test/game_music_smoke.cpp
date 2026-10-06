#include "audiodecode.h"
#include "visualization.h"
#include "libretro.h"
#include "game_music_fixtures.h"
#include "gme/gme/gme.h"
#include "libvgm/player/playera.hpp"
#include "libvgm/player/vgmplayer.hpp"
#include "libvgm/utils/MemoryLoader.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

static void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}
static size_t delivered = 0;
static size_t batch(const int16_t *, size_t frames)
{
    delivered += frames;
    return frames;
}
retro_audio_sample_batch_t audio_batch_cb = batch;
retro_audio_sample_t audio_cb = nullptr;

static void check_audio(const char *filename, unsigned duration, bool native_loop = true)
{
    float rate = 0;
    std::unique_ptr<auddecode> decoder(make_decoder(filename, &rate));
    check(decoder && rate == 44100, "game music opens at output rate");
    check(decoder->song_duration() == duration, "game music reports duration");
    double energy = 0;
    uint64_t total = 0;
    for (unsigned i = 0; i < 1000 && decoder->is_playing(); ++i)
    {
        unsigned frames = 997;
        float *samples = nullptr;
        decoder->mix(samples, frames);
        check(frames <= 997 && (!frames || samples), "game music frame contract");
        total += frames;
        for (unsigned j = 0; j < frames * 2; ++j)
        {
            check(std::isfinite(samples[j]), "game music produces finite samples");
            energy += std::abs(samples[j]);
        }
    }
    check(energy > 1, "game music is audible");
    check(!decoder->is_playing() && total == uint64_t(duration) * 44100 / 1000, "single pass has exact frame count");
    check(decoder->seek(0), "game music rewinds after EOF");
    decoder->set_loop(true);
    check(decoder->seek(duration - 10), "game music seeks near loop boundary");
    unsigned frames = 4096;
    float *samples = nullptr;
    decoder->mix(samples, frames);
    check(frames == 4096 && decoder->is_playing(), "game music loops across duration without a gap");
    decoder->set_loop(false);
    frames = 512;
    decoder->mix(samples, frames);
    if (native_loop)
        check(!frames && !decoder->is_playing(), "disabling native loop stops after duration");
    else
    {
        // A VGM without a loop command restarts from the beginning at EOF.
        // Disabling Repeat finishes that new pass.
        for (unsigned i = 0; i < 100 && decoder->is_playing(); ++i)
        {
            frames = 512;
            decoder->mix(samples, frames);
        }
        check(!decoder->is_playing(), "disabling repeat finishes the current one-shot VGM pass");
    }
}

static void check_external(const std::filesystem::path &path)
{
    const auto filename = path.u8string();
    std::printf("Checking %s\n", reinterpret_cast<const char *>(filename.c_str()));
    std::fflush(stdout);
    float rate = 0;
    std::unique_ptr<auddecode> decoder;
    try
    {
        decoder.reset(make_decoder(reinterpret_cast<const char *>(filename.c_str()), &rate));
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "%s\n", e.what());
    }
    check(decoder && rate == 44100, "external game music opens");
    const unsigned tracks = decoder->track_count();
    check(tracks > 0, "external file exposes tracks");
    unsigned audible = 0;
    for (unsigned track = 0; track < tracks; ++track)
    {
        if (track)
            check(decoder->select_track(track), "external archive track loads");
        double energy = 0;
        unsigned rendered = 0;
        for (unsigned block = 0; block < 130; ++block)
        {
            float *samples = nullptr;
            unsigned frames = 1024;
            decoder->mix(samples, frames);
            check(frames <= 1024 && (!frames || samples), "external frame contract");
            rendered += frames;
            for (unsigned i = 0; i < frames * 2; ++i)
            {
                check(std::isfinite(samples[i]), "external samples are finite");
                energy += std::abs(samples[i]);
            }
            if (!frames)
                break;
        }
        check(rendered > 0, "external track renders audio frames");
        if (energy > 0.01)
            ++audible;
        check(decoder->seek(0) && decoder->seek(1000), "external track supports rewind and seek");
        std::printf("  %u/%u: %s (%u ms, energy %.2f)\n", track + 1, tracks,
                    decoder->song_title() ? decoder->song_title() : "", decoder->song_duration(), energy);
        std::fflush(stdout);
    }
    check(audible > 0, "external file produces audible audio");
    if (tracks > 1)
        check(decoder->select_track(0), "return to first external track");
    const auto duration = decoder->song_duration();
    if (duration > 10)
    {
        decoder->set_loop(true);
        check(decoder->seek(duration - 10), "external track seeks to loop boundary");
        float *samples = nullptr;
        unsigned frames = 4096;
        decoder->mix(samples, frames);
        check(frames == 4096, "external track continues across loop boundary");
        decoder->set_loop(false);
        frames = 512;
        decoder->mix(samples, frames);
        check(!frames && !decoder->is_playing(), "external track stops when looping is disabled");
    }
    std::printf("Passed: %u tracks decoded, %u audible in their first three seconds.\n", tracks, audible);
}

static void check_native_loop(const char *filename, const game_fixtures::bytes &data, bool is_gme)
{
    game_fixtures::save(filename, data);
    float rate = 0;
    std::unique_ptr<auddecode> decoder(make_decoder(filename, &rate));
    check(decoder != nullptr, "native comparison decoder opens");
    decoder->set_loop(true);
    std::unique_ptr<Music_Emu, decltype(&gme_delete)> gme(nullptr, gme_delete);
    std::unique_ptr<DATA_LOADER, decltype(&DataLoader_Deinit)> loader(nullptr, DataLoader_Deinit);
    std::unique_ptr<PlayerA> vgm;
    if (is_gme)
    {
        gme.reset(gme_new_emu(gme_spc_type, 44100));
        check(gme && !gme_load_data(gme.get(), data.data(), long(data.size())), "reference GME opens");
        gme_set_autoload_playback_limit(gme.get(), 0);
        gme_ignore_silence(gme.get(), 1);
        check(!gme_start_track(gme.get(), 0), "reference GME starts");
        gme_set_fade_msecs(gme.get(), -1, 0);
    }
    else
    {
        loader.reset(MemoryLoader_Init(data.data(), UINT32(data.size())));
        check(loader && !DataLoader_Load(loader.get()), "reference VGM data opens");
        vgm = std::make_unique<PlayerA>();
        vgm->RegisterPlayerEngine(new VGMPlayer);
        check(!vgm->SetOutputSettings(44100, 2, 16, 4096) && !vgm->LoadFile(loader.get()), "reference VGM opens");
        vgm->SetLoopCount(0);
        vgm->SetFadeSamples(0);
        vgm->SetEndSilenceSamples(0);
        check(!vgm->Start(), "reference VGM starts");
    }
    uint64_t later_energy = 0;
    for (unsigned block = 0; block < 1000; ++block)
    {
        if (block == 400)
        {
            decoder->set_loop(false);
            float *samples = nullptr;
            unsigned frames = 128;
            decoder->mix(samples, frames);
            check(!frames && !decoder->is_playing(), "Repeat off stops beyond metadata duration");
            decoder->set_loop(true);
        }
        int16_t reference[2048];
        if (is_gme)
            check(!gme_play(gme.get(), 2048, reference), "reference GME renders");
        else
            check(vgm->Render(sizeof(reference), reference) == sizeof(reference), "reference VGM renders");
        float *samples = nullptr;
        unsigned frames = 1024;
        decoder->mix(samples, frames);
        check(frames == 1024 && decoder->is_playing(), "native Repeat fills every block, including after EOF");
        for (unsigned i = 0; i < frames * 2; ++i)
        {
            check(samples[i] == reference[i] / 32768.0f, "Repeat preserves native audio and loop phase without rewinding");
            if (block > 700)
                later_energy += std::abs(int(reference[i]));
        }
    }
    check(later_energy > 0, "native song remains audible after multiple loops or its long quiet passage");
    decoder.reset();
    std::filesystem::remove(filename);
}

int main(int argc, char **argv)
{
    if (argc == 3 && std::string(argv[1]) == "--reject-rar5")
    {
        check(!music_play(argv[2]), "unsupported RAR5 archive rejected");
        check(music_error().find("RAR5 archives are not supported") != std::string::npos,
              "RAR5 rejection explains the format limitation");
        check(!music_isplaying() && music_trackcount() == 0, "RAR5 rejection leaves player stopped");
        return 0;
    }
    if (argc > 1)
    {
        unsigned checked = 0;
        for (int i = 1; i < argc; ++i)
        {
            const std::filesystem::path path(argv[i]);
            if (std::filesystem::is_directory(path))
            {
                for (const auto &entry : std::filesystem::directory_iterator(path))
                {
                    auto ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                                   { return std::tolower(c); });
                    if (entry.is_regular_file() && (ext == ".rsn" || ext == ".vgm" || ext == ".vgz" || ext == ".spc"))
                    {
                        check_external(entry.path());
                        ++checked;
                    }
                }
            }
            else
            {
                check_external(path);
                ++checked;
            }
        }
        check(checked > 0, "external game music fixtures found");
        return 0;
    }
    using namespace game_fixtures;
    save("generated-game.VGM", vgm());
    save("generated-game.vgz", gzip(vgm()));
    save("generated-game.SPC", spc());
    save("generated-game.nsf", nsf());
    save("generated-game.rsn", rsn({{"02 - Second.SPC", spc("Second", 0x800)},
                                    {"notes.txt", {'h', 'i'}},
                                    {"folder/01 - First.spc", spc("First")}}));
    for (const char *ext : {"vgm", "vgz", "spc", "rsn", "ay", "gbs", "gym", "hes", "kss", "nsf", "nsfe", "sap"})
        check(auddecode_supports((std::string("file.") + ext).c_str()), "new format advertised");
    check_audio("generated-game.VGM", 1000);
    check_audio("generated-game.vgz", 1000);
    check_audio("generated-game.SPC", 2000);
    music_repeat(true);
    check(music_play("generated-game.vgz") && music_islooping(), "player enables native game-music looping");
    check(music_title() == "Generated VGM", "VGM GD3 title");
    delivered = 0;
    for (int i = 0; i < 180; ++i)
        music_run();
    check(delivered == 132300 && music_getposition() == 3000 && music_isplaying(), "VGM runs for multiple native loops");
    music_repeat(false);
    music_stop();

    check(music_play("generated-game.rsn"), "RSN opens through unarr and GME");
    check(music_trackcount() == 2 && music_currenttrack() == 0, "RSN exposes every SPC and ignores non-SPC files");
    check(music_title() == "Second" && music_tracktitle(0) == "02 - Second.SPC", "RSN filenames and SPC titles");
    music_run();
    music_pause(true);
    check(music_settrack(1) && music_title() == "First", "RSN switches to another archive member");
    check(music_ispaused() && music_getposition() == 0, "track switch preserves pause and resets position");
    check(music_visualization().left == visualization_data{}.left, "track switch clears output history");
    check(!music_settrack(2) && music_currenttrack() == 1, "invalid track selection preserves current track");
    music_pause(false);
    music_setposition(1000);
    music_run();
    check(music_getposition() == 1016, "RSN SPC seeking");
    music_stop();
    check(music_trackcount() == 0 && music_title().empty(), "unload clears archive state");
    save("generated-solid.rsn", rsn({{"notes.txt", {'h', 'i'}}, {"01.spc", spc("First")}, {"02.spc", spc("Second", 0x800)}}, true));
    check(music_play("generated-solid.rsn") && music_trackcount() == 2, "RAR4 solid archive header accepted");
    check(music_settrack(1) && music_title() == "Second", "solid RSN exposes later SPC members");
    music_run();
    music_stop();
    check(music_play("generated-game.nsf") && music_trackcount() == 2, "GME multi-track NSF");
    check(music_settrack(1) && music_currenttrack() == 1, "GME subsong selection");
    music_run();
    music_stop();

    // Loop beyond metadata duration through the player API, including seeking,
    // subsong selection and enabling Repeat again after a completed track.
    for (const char *file : {"generated-game.SPC", "generated-game.rsn", "generated-game.nsf", "generated-game.vgz"})
    {
        music_repeat(true);
        check(music_play(file) && music_islooping(), "game music enables indefinite repeat");
        if (music_trackcount() > 1)
            check(music_settrack(1), "repeat survives subsong selection");
        const auto duration = music_getduration();
        music_setposition(duration - 10);
        delivered = 0;
        for (int i = 0; i < 180; ++i)
            music_run();
        check(delivered == 132300 && music_isplaying() && music_getposition() > duration,
              "GME and VGM continue beyond metadata duration");
        music_setposition(0);
        music_run();
        check(music_isplaying() && music_islooping(), "rewinding preserves indefinite repeat");
        music_repeat(false);
        music_setposition(duration);
        music_run();
        music_run();
        check(!music_isplaying(), "disabling repeat permits EOF");
        music_repeat(true);
        music_run();
        check(music_isplaying() && music_getposition() == duration + 16, "enabling repeat after EOF preserves elapsed time");
        music_stop();
        music_repeat(false);
    }
    auto one_shot = vgm();
    put(one_shot, 0x1c, 0);
    put(one_shot, 0x20, 0);
    save("generated-one-shot.vgm", one_shot);
    check_audio("generated-one-shot.vgm", 1000, false);
    std::filesystem::remove("generated-one-shot.vgm");
    auto quiet_spc = spc("Long quiet passage");
    // Key on, wait about 0.4 seconds, key off, wait about nine seconds, then
    // key on again. The quiet section must not cause an automatic rewind.
    const uint8_t quiet_code[] = {0x8f, 0x4c, 0xf2, 0x8f, 0x01, 0xf3,
                                  0x8d, 0xff, 0xcd, 0xff, 0x1d, 0xd0, 0xfd, 0xfe, 0xf9,
                                  0x8f, 0x5c, 0xf2, 0x8f, 0x01, 0xf3,
                                  0xe8, 0x18, 0x8d, 0xff, 0xcd, 0xff, 0x1d, 0xd0, 0xfd, 0xfe, 0xf9, 0x9c, 0xd0, 0xf4,
                                  0x8f, 0x5c, 0xf2, 0x8f, 0x00, 0xf3, 0x8f, 0x4c, 0xf2, 0x8f, 0x01, 0xf3, 0x2f, 0xfe};
    std::memcpy(quiet_spc.data() + 0x300, quiet_code, sizeof(quiet_code));
    check_native_loop("generated-native.spc", quiet_spc, true);
    check_native_loop("generated-native.vgm", vgm(), false);

    for (const char *ext : {"vgm", "vgz", "spc", "rsn", "ay", "gbs", "gym", "hes", "kss", "nsf", "nsfe", "sap"})
    {
        const auto filename = "generated-invalid." + std::string(ext);
        save(filename, {'b', 'a', 'd'});
        check(!music_play(filename.c_str()), "invalid game music rejected");
        std::filesystem::remove(filename);
    }
    save("generated-invalid.rsn", rsn({{"readme.txt", {'x'}}}));
    check(!music_play("generated-invalid.rsn") && music_error().find("no valid SPC") != std::string::npos, "empty RSN error");
    auto damaged = rsn({{"song.spc", spc()}});
    damaged.resize(damaged.size() / 2);
    save("generated-invalid.rsn", damaged);
    check(!music_play("generated-invalid.rsn"), "truncated RSN rejected");
    damaged = gzip(vgm());
    damaged.resize(damaged.size() - 5);
    save("generated-invalid.vgz", damaged);
    check(!music_play("generated-invalid.vgz"), "truncated VGZ rejected");
    damaged = vgm();
    put(damaged, 0x34, 0xfffffff0);
    save("generated-invalid.vgm", damaged);
    check(!music_play("generated-invalid.vgm"), "VGM rejects out-of-range data offset");
    damaged = vgm();
    damaged[0x106] = 0x66;
    save("generated-invalid.vgm", damaged);
    check(!music_play("generated-invalid.vgm"), "VGM rejects a loop without a wait even if header claims duration");
    damaged = vgm();
    damaged.resize(0x108);
    put(damaged, 4, uint32_t(damaged.size() - 4));
    put(damaged, 0x14, 0);
    save("generated-invalid.vgm", damaged);
    check(!music_play("generated-invalid.vgm"), "VGM rejects truncated commands");
    // Paths in an archive are display names, never filesystem destinations.
    save("generated-invalid.rsn", rsn({{"../generated-escape.spc", spc()}}));
    check(music_play("generated-invalid.rsn"), "RSN reads path-like member names in memory");
    check(!std::filesystem::exists("../generated-escape.spc"), "RSN does not extract members to disk");
    music_stop();
    for (const char *file : {"generated-game.VGM", "generated-game.vgz", "generated-game.SPC", "generated-game.nsf",
                             "generated-game.rsn", "generated-solid.rsn", "generated-invalid.rsn", "generated-invalid.vgz", "generated-invalid.vgm"})
        std::filesystem::remove(file);
    std::puts("VGM, VGZ, SPC, RSN and GME track checks passed.");
}
