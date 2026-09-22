#pragma once
#include "decoder_base.h"
#include <array>
#include <cctype>
#include <stdexcept>
#include <zlib.h>

inline std::string game_music_extension(const std::string &name)
{
    const auto dot = name.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                   { return std::tolower(c); });
    return ext;
}

inline std::vector<uint8_t> read_game_music(const char *filename)
{
    constexpr size_t limit = 256 * 1024 * 1024;
    const auto path = std::filesystem::path(reinterpret_cast<const char8_t *>(filename));
    if (std::filesystem::file_size(path) > limit)
        throw std::runtime_error("Game music file exceeds the 256 MiB limit.");
    auto bytes = read_audio_file(filename);
    if (bytes.size() < 2 || bytes[0] != 0x1f || bytes[1] != 0x8b)
        return bytes;
    z_stream stream{};
    if (inflateInit2(&stream, 15 + 16) != Z_OK)
        throw std::runtime_error("Unable to initialize gzip decompression.");
    struct cleanup
    {
        z_stream *s;
        ~cleanup() { inflateEnd(s); }
    } cleanup{&stream};
    stream.next_in = bytes.data();
    stream.avail_in = static_cast<uInt>(bytes.size());
    std::vector<uint8_t> output;
    std::array<uint8_t, 32768> block;
    int status;
    do
    {
        stream.next_out = block.data();
        stream.avail_out = static_cast<uInt>(block.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END)
            throw std::runtime_error("Invalid or truncated gzip music file.");
        const size_t got = block.size() - stream.avail_out;
        if (got > limit - output.size())
            throw std::runtime_error("Decompressed music exceeds the 256 MiB limit.");
        output.insert(output.end(), block.begin(), block.begin() + got);
    } while (status != Z_STREAM_END);
    if (stream.avail_in)
        throw std::runtime_error("Unexpected data after gzip music stream.");
    return output;
}
