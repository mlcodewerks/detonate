#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace audiotags
{

    enum class Format
    {
        unknown,
        mpeg_audio,
        flac,
        ogg_vorbis,
        opus,
        mp4,
        matroska,
        mod,
        s3m,
        xm,
        it,
        monkeys_audio,
        wave,
        future_composer
    };

    struct Picture
    {
        // ID3/FLAC picture type. 3 = front cover, 4 = back cover.
        std::uint32_t type = 0;
        // MIME type, or "-->" when the tag contains an external image URL.
        std::string mime;
        std::string description;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t depth = 0;
        std::uint32_t colors = 0;
        std::vector<std::uint8_t> data;
    };

    struct Metadata
    {
        Format format = Format::unknown;

        std::string title;
        std::string artist;
        std::string album;
        std::string album_artist;
        std::string composer;
        std::string genre;
        std::string date;
        std::string comment;

        std::uint32_t track_number = 0;
        std::uint32_t track_total = 0;
        std::uint32_t disc_number = 0;
        std::uint32_t disc_total = 0;

        double duration_seconds = 0.0;
        std::uint32_t sample_rate = 0;
        std::uint32_t channels = 0;
        std::uint32_t bits_per_sample = 0;
        std::uint32_t bitrate_kbps = 0;

        std::unordered_map<std::string, std::vector<std::string>> tags;
        std::vector<Picture> pictures;

        void clear();
        const std::vector<std::string> *values(const std::string &key) const;
        std::string value(const std::string &key, std::size_t index = 0) const;
        const Picture *front_cover() const;
    };

    bool read_file(const std::string &path, Metadata &out, std::string *error = nullptr);

    // Parses from an in-memory complete audio file image.
    bool read_memory(const void *data,
                     std::size_t size,
                     Metadata &out,
                     std::string *error = nullptr);

    const char *format_name(Format format);

} // namespace audiotags
