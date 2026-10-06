#include "file_browser.h"
#include "audiodecode.h"
#include "libretro.h"
#include "game_music_fixtures.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}
static uint64_t energy = 0, frames_out = 0;
static size_t batch(const int16_t *samples, size_t frames)
{
    frames_out += frames;
    for (size_t i = 0; i < frames * 2; ++i)
        energy += std::abs(int(samples[i]));
    return frames;
}
retro_audio_sample_batch_t audio_batch_cb = batch;
retro_audio_sample_t audio_cb = nullptr;
static size_t find(const file_browser &browser, const char *name)
{
    for (size_t i = 0; i < browser.entries().size(); ++i)
        if (browser.entries()[i].name == name)
            return i;
    std::fprintf(stderr, "Missing entry %s at %s: %s\n", name, browser.location().c_str(), browser.error().c_str());
    std::exit(1);
}
static void audible()
{
    energy = frames_out = 0;
    for (int i = 0; i < 10; ++i)
        music_run();
    check(energy > 0 && frames_out == 7350, "selected archive member plays audible stereo audio");
}
static game_fixtures::bytes directory_zip(unsigned count, bool malformed_extra = false)
{
    using namespace game_fixtures;
    bytes data;
    std::vector<size_t> offsets;
    for (unsigned i = 0; i < count; ++i)
    {
        offsets.push_back(data.size());
        const auto pos = data.size();
        data.resize(pos + 32);
        put(data, pos, 0x04034b50);
        put(data, pos + 4, 20, 2);
        put(data, pos + 26, 2, 2);
        text(data, pos + 30, "d/");
    }
    const auto central = data.size();
    for (unsigned i = 0; i < count; ++i)
    {
        const auto pos = data.size();
        data.resize(pos + 48 + (malformed_extra ? 9 : 0));
        put(data, pos, 0x02014b50);
        put(data, pos + 4, 20, 2);
        put(data, pos + 6, 20, 2);
        put(data, pos + 28, 2, 2);
        put(data, pos + 38, 0x10);
        put(data, pos + 42, uint32_t(offsets[i]));
        text(data, pos + 46, "d/");
        if (malformed_extra)
        {
            put(data, pos + 24, 0xffffffff);
            put(data, pos + 30, 9, 2);
            put(data, pos + 48, 1, 2); // ZIP64
            put(data, pos + 50, 8, 2); // Eight bytes declared, only five present.
        }
    }
    const auto end = data.size();
    data.resize(end + 22);
    put(data, end, 0x06054b50);
    put(data, end + 8, count, 2);
    put(data, end + 10, count, 2);
    put(data, end + 12, uint32_t(end - central));
    put(data, end + 16, uint32_t(central));
    return data;
}
int main(int argc, char **argv)
{
    check(argc == 2, "fixture directory argument");
    const std::filesystem::path fixtures(argv[1]);
    check(is_music_archive("A.ZIP") && is_music_archive("a.rar") && is_music_archive("a.7z") &&
              is_music_archive("a.RSN") && !is_music_archive("a.vgz"),
          "archive extensions exclude VGZ");
    for (const char *file : {"browser.zip", "browser.7z"})
    {
        file_browser browser;
        check(browser.open(fixtures), "open filesystem directory");
        check(browser.activate(find(browser, file)) && browser.in_archive(), "enter archive from filesystem listing");
        check(browser.entries().size() == 2, "archive shows implicit folders and music, filtering text files");
        check(browser.activate(find(browser, "02.spc")) && music_title() == "Second", "select root archive member");
        audible();
        check(browser.activate(find(browser, "folder")), "enter archive folder");
        check(browser.activate(find(browser, "01.spc")) && music_title() == "First", "select nested member");
        audible();
        const auto location = browser.location();
        check(!browser.open(fixtures / "solid-rar5.rsn") && browser.location() == location,
              "unsupported archive preserves current browser location");
        check(browser.error().find("RAR5") != std::string::npos, "archive failure explains RAR5 limitation");
        browser.refresh();
        const auto vgz = find(browser, "sample.vgz");
        check(!browser.entries()[vgz].archive && browser.activate(vgz) && music_title() == "Generated VGM",
              "VGZ is played directly rather than browsed");
        audible();
        check(browser.activate(find(browser, "nested.rsn")), "enter RSN nested inside ZIP or 7z");
        check(browser.activate(find(browser, "inside.spc")) && music_title() == "Nested", "play nested RSN member");
        browser.up();
        check(browser.location() == location, "Up returns to the containing archive folder");
        browser.up();
        browser.up();
        check(!browser.in_archive(), "Up exits archive to filesystem");
        audible(); // Playback keeps its temporary file when the browser releases archive data.
        music_stop();
    }
    using namespace game_fixtures;
    const auto songs = std::filesystem::absolute("generated-subsong-browser");
    std::filesystem::create_directory(songs);
    save(songs / "music.nsf", nsf());
    save(songs / "music.nsfe", nsfe());
    auto single = nsf(); single[6] = 1;
    save(songs / "single.nsf", single);
    save(songs / "music.rsn", rsn({{"folder/music.nsfe", nsfe()}}));
    {
        file_browser subsongs;
        check(subsongs.open(songs), "open subsong directory");
        check(subsongs.entries()[find(subsongs, "music.nsf")].archive &&
            !subsongs.entries()[find(subsongs, "single.nsf")].archive,
            "only files with multiple subsongs are containers");
        check(subsongs.activate(find(subsongs, "music.nsf")) && subsongs.in_subsongs(), "enter NSF subsongs");
        check(subsongs.entries().size() == 2 && subsongs.entries()[1].track == 1, "NSF subsongs in native order");
        check(subsongs.activate(1) && music_currenttrack() == 1, "play second NSF subsong");
        subsongs.up();
        check(!subsongs.in_subsongs(), "Up exits NSF");
        check(subsongs.open(songs / "music.nsfe") && subsongs.in_subsongs(), "direct NSFE open browses subsongs");
        const auto &first = subsongs.entries()[0].tags;
        const auto &second = subsongs.entries()[1].tags;
        check(first.title == "First" && second.title == "Second" && second.artist == "Artist" &&
            second.album == "Game" && first.duration == 1000 && second.duration == 2000,
            "read each subsong's tags and duration");
        subsongs.refresh();
        check(subsongs.entries().size() == 2, "refresh preserves subsong view");
        auto items = subsongs.playlist();
        check(items.size() == 2 && items[0].key != items[1].key && items[1].track == 1 &&
            items[0].source == items[1].source, "playlist retains unique identity and shared source");
        music_play_async(items[1].source, items[1].track); music_wait(); music_poll();
        check(music_currenttrack() == 1 && music_title() == "Artist [Game] Second", "async playlist selects subsong atomically");
        check(subsongs.open(songs / "music.rsn"), "open archive with subsong file");
        check(music_title() == "Artist [Game] Second" && music_currenttrack() == 1, "probing preserves playback");
        check(subsongs.activate(find(subsongs, "folder")) &&
            subsongs.activate(find(subsongs, "music.nsfe")), "enter archived subsongs");
        items = subsongs.playlist();
        const auto location = subsongs.location();
        check(!subsongs.open(songs / "missing.zip") && subsongs.location() == location,
            "failed open preserves subsong listing");
        subsongs.up(); subsongs.up(); subsongs.up();
        check(!subsongs.in_subsongs() && !subsongs.in_archive(), "Up unwinds subsongs, folder and archive");
        music_play_archive_async(items[1].members, items[1].member, items[1].track);
        items.clear(); music_wait(); music_poll();
        check(music_currenttrack() == 1 && music_title() == "Artist [Game] Second", "archived subsong survives navigation");
        music_stop();
    }
    for (const auto *name : {"music.nsf", "music.nsfe", "single.nsf", "music.rsn"})
        std::filesystem::remove(songs / name);
    std::filesystem::remove(songs);
    check(read_music_archive(directory_zip(50000), "directories.zip").empty(),
          "long directory runs do not exhaust the parser stack");
    bool rejected = false;
    try { read_music_archive(directory_zip(1, true), "bad-zip64.zip"); }
    catch (const std::exception &) { rejected = true; }
    check(rejected, "truncated ZIP64 extra field is rejected before reading beyond its buffer");
    const auto payload = rsn({{"folder/01.spc", spc("RAR track")}, {"../escape.spc", spc()}, {"notes.txt", {'x'}}}, true);
    for (const char *file : {"generated-browser.rar", "generated-browser.rsn"})
    {
        save(file, payload);
        file_browser browser;
        check(browser.open(file) && browser.entries().size() == 1, "RAR and RSN browse safely with traversal entries hidden");
        check(browser.activate(find(browser, "folder")) && browser.activate(find(browser, "01.spc")), "RAR folder and SPC selection");
        check(music_title() == "RAR track", "selected RAR member metadata");
        audible();
        music_stop();
        std::filesystem::remove(file);
    }
    save("generated-bad.zip", {'b', 'a', 'd'});
    file_browser browser;
    check(!browser.open("generated-bad.zip") && !browser.error().empty(), "corrupt archive produces browser error");
    std::filesystem::remove("generated-bad.zip");
    // Leaving an archive on a removed drive/directory must retain its listing.
    const auto parent = std::filesystem::absolute("generated-browser-parent");
    check(std::filesystem::create_directory(parent), "create temporary archive directory");
    save(parent / "music.rsn", rsn({{"track.spc", spc("Still available")}}));
    check(browser.open(parent / "music.rsn"), "open archive before parent disappears");
    const auto previous_location = browser.location();
    std::filesystem::remove(parent / "music.rsn");
    std::filesystem::remove(parent);
    browser.up();
    check(browser.in_archive() && browser.location() == previous_location && !browser.error().empty(),
          "failed Up preserves archive and location");
    check(browser.activate(find(browser, "track.spc")) && music_title() == "Still available",
          "failed Up preserves usable member indices");
    music_stop();
    std::puts("Archive navigation, member playback, nesting, and error checks passed.");
}
