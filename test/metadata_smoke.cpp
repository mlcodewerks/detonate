#include "audiodecode.h"
#include "decoder_base.h"
#include "game_music_fixtures.h"
#include "libretro.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

retro_audio_sample_batch_t audio_batch_cb = nullptr;
retro_audio_sample_t audio_cb = nullptr;
using namespace game_fixtures;
static void check(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static std::string utf8(const std::filesystem::path &path)
{
    const auto name = path.u8string();
    return {name.begin(), name.end()};
}
static void append32(bytes &data, uint32_t value)
{
    const auto pos = data.size(); data.resize(pos + 4); put(data, pos, value);
}
static void append_text(bytes &data, const std::string &value)
{
    data.insert(data.end(), value.begin(), value.end());
}
static bytes ape(bytes data)
{
    const auto start = data.size();
    for (const auto &[key, value] : {std::pair{"Title", "Song"}, {"Artist", "Artist"}, {"Album", "Album"}})
    {
        append32(data, uint32_t(std::strlen(value))); append32(data, 0);
        append_text(data, key); data.push_back(0); append_text(data, value);
    }
    const auto footer = data.size(); data.resize(footer + 32);
    text(data, footer, "APETAGEX"); put(data, footer + 8, 2000);
    put(data, footer + 12, uint32_t(data.size() - start)); put(data, footer + 16, 3);
    return data;
}
static bytes id3(bytes data)
{
    if (data.size() >= 10 && !std::memcmp(data.data(), "ID3", 3))
    {
        const size_t size = 10 + (size_t(data[6]) << 21) + (size_t(data[7]) << 14) + (size_t(data[8]) << 7) + data[9];
        check(size <= data.size(), "fixture ID3 size");
        data.erase(data.begin(), data.begin() + size);
    }
    bytes tag(10); text(tag, 0, "ID3"); tag[3] = 3;
    for (const auto &[key, value] : {std::pair{"TIT2", "Song"}, {"TPE1", "Artist"}, {"TALB", "Album"}})
    {
        const auto pos = tag.size(); tag.resize(pos + 11);
        text(tag, pos, key); tag[pos + 7] = uint8_t(std::strlen(value) + 1);
        append_text(tag, value);
    }
    const auto size = tag.size() - 10;
    for (unsigned i = 0; i < 4; ++i) tag[9 - i] = uint8_t((size >> (7 * i)) & 127);
    tag.insert(tag.end(), data.begin(), data.end());
    return tag;
}
static bytes flac_tags(const bytes &data, bool title_only = false)
{
    check(data.size() > 42 && !std::memcmp(data.data(), "fLaC", 4), "FLAC fixture");
    bytes result(data.begin(), data.begin() + 42); result[4] = 0;
    size_t pos = 4;
    for (;;)
    {
        check(pos + 4 <= data.size(), "FLAC block header");
        const bool last = data[pos] & 128;
        const size_t size = size_t(data[pos + 1]) * 65536 + size_t(data[pos + 2]) * 256 + data[pos + 3];
        pos += 4 + size; check(pos <= data.size(), "FLAC block size");
        if (last) break;
    }
    bytes comments; append32(comments, 0); append32(comments, title_only ? 1 : 3);
    for (const auto *value : {"TITLE=Song", "ARTIST=Artist", "ALBUM=Album"})
    {
        append32(comments, uint32_t(std::strlen(value))); append_text(comments, value);
        if (title_only) break;
    }
    result.insert(result.end(), {0x84, 0, 0, uint8_t(comments.size())});
    result.insert(result.end(), comments.begin(), comments.end());
    result.insert(result.end(), data.begin() + pos, data.end());
    return result;
}
static bytes wave_tags(bytes data)
{
    bytes info; append_text(info, "INFO");
    for (const auto &[key, value] : {std::pair{"INAM", "Song"}, {"IART", "Artist"}, {"IPRD", "Album"}})
    {
        append_text(info, key); append32(info, uint32_t(std::strlen(value) + 1));
        append_text(info, value); info.push_back(0);
        if (info.size() & 1) info.push_back(0);
    }
    append_text(data, "LIST"); append32(data, uint32_t(info.size()));
    data.insert(data.end(), info.begin(), info.end()); put(data, 4, uint32_t(data.size() - 8));
    return data;
}
int main(int argc, char **argv)
{
    check(argc == 2, "source directory");
    const auto fixture = [&](const char *extension) {
        return read_audio_file(utf8(std::filesystem::path(argv[1]) / "test" / (std::string("test.") + extension)).c_str());
    };
    for (const char *extension : {"flac", "mp3", "wv", "mpc", "wav"})
    {
        auto data = fixture(extension);
        check(!data.empty(), "read streamed fixture");
        if (!std::strcmp(extension, "flac")) data = flac_tags(data);
        else if (!std::strcmp(extension, "mp3")) data = id3(data);
        else if (!std::strcmp(extension, "wav")) data = wave_tags(data);
        else data = ape(data);
        const auto path = std::filesystem::path(u8"generated-métadata." + std::u8string(reinterpret_cast<const char8_t *>(extension)));
        save(path, data);
        float rate = 0;
        std::unique_ptr<auddecode> decoder(make_decoder(utf8(path).c_str(), &rate));
        check(decoder && std::string(decoder->song_title()) == "Song", extension);
        check(std::string(decoder->song_artist()) == "Artist" && std::string(decoder->song_album()) == "Album", "raw artist and album");
        decoder.reset();
        check(music_play(utf8(path).c_str()) && music_title() == "Artist [Album] Song", "formatted streamed title");
        music_stop();
        check(music_play_memory(std::string("folder/song.") + extension, data) && music_title() == "Artist [Album] Song", "archived tags");
        music_stop(); std::filesystem::remove(path);
    }
    check(music_play_memory("title-only.flac", flac_tags(fixture("flac"), true)) && music_title() == "Song", "title-only tags");
    music_stop();
    auto malformed = fixture("wav");
    append_text(malformed, "LIST"); append32(malformed, 12);
    append_text(malformed, "INFOINAM"); append32(malformed, 0xffffffff);
    put(malformed, 4, uint32_t(malformed.size() - 8));
    check(music_play_memory("bad-tags.wav", malformed) && music_title() == "bad-tags", "malformed optional tags do not prevent playback");
    music_stop();
    check(music_play_memory("folder/original-name.wav", fixture("wav")) && music_title() == "original-name", "untagged archive uses original filename");
    music_stop(); check(music_title().empty(), "Stop clears title");
    for (const char *extension : {"mod", "s3m", "xm", "it", "ogg", "opus", "m4a"})
    {
        auto data = fixture(extension);
        audiotags::Metadata tags;
        check(audiotags::read_memory(data.data(), data.size(), tags), "recognize metadata container");
        check(music_play_memory(std::string("sample.") + extension, data), "metadata does not break playback");
        if (!tags.title.empty()) check(music_title().find(tags.title) != std::string::npos, "use audiotags song title");
        music_stop();
    }
    auto tagged_spc = spc("Native song");
    text(tagged_spc, 0x4e, "Native game"); text(tagged_spc, 0xb1, "Native artist");
    check(music_play_memory("native.spc", tagged_spc) && music_title() == "Native artist [Native game] Native song", "native emulator tags");
    music_stop();
    check(music_play_memory("native.rsn", rsn({{"01.spc", tagged_spc}, {"02.spc", spc("Second")}})), "tagged RSN");
    check(music_title() == "Native artist [Native game] Native song", "RSN native tags");
    check(music_settrack(1) && music_title() == "Second", "track changes clear previous tags");
    music_stop();
    auto tagged_vgm = vgm();
    const auto gd3 = 0x14 + uint32_t(tagged_vgm[0x14]) + (uint32_t(tagged_vgm[0x15]) << 8) +
                     (uint32_t(tagged_vgm[0x16]) << 16) + (uint32_t(tagged_vgm[0x17]) << 24);
    tagged_vgm.resize(gd3 + 12);
    for (const auto *field : {"Native VGM", "", "Native game", "", "", "", "Native artist", "", "", "", ""})
    {
        for (const char *p = field; *p; ++p) { tagged_vgm.push_back(uint8_t(*p)); tagged_vgm.push_back(0); }
        tagged_vgm.insert(tagged_vgm.end(), {0, 0});
    }
    put(tagged_vgm, gd3 + 8, uint32_t(tagged_vgm.size() - gd3 - 12));
    put(tagged_vgm, 4, uint32_t(tagged_vgm.size() - 4));
    check(music_play_memory("native.vgz", gzip(tagged_vgm)) && music_title() == "Native artist [Native game] Native VGM", "native GD3 tags through VGZ");
    music_stop();
    std::puts("Streamed metadata, native tags, Unicode paths, archive titles and fallback checks passed.");
}
