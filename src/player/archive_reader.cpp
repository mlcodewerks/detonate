#include "archive_reader.h"
#include "formats/game_music_io.h"
#include <unarr.h>
#include <cstring>
#include <memory>

bool is_music_archive(const std::string &name)
{
    const auto ext = game_music_extension(name);
    return ext == "zip" || ext == "rar" || ext == "7z" || ext == "rsn";
}

std::vector<archive_member> read_music_archive(const std::vector<uint8_t> &bytes,
                                               const std::string &name, size_t member_limit)
{
    constexpr size_t max_total = 256 * 1024 * 1024;
    if (bytes.size() > max_total)
        throw std::runtime_error("Archive exceeds the 256 MiB input limit.");
    if (bytes.size() >= 8 && !std::memcmp(bytes.data(), "Rar!\x1a\x07\x01\x00", 8))
        throw std::runtime_error("RAR5 archives are not supported. Use RAR4, ZIP, 7z or individual music files.");
    std::unique_ptr<ar_stream, decltype(&ar_close)> stream(ar_open_memory(bytes.data(), bytes.size()), ar_close);
    if (!stream)
        throw std::bad_alloc();
    const auto ext = game_music_extension(name);
    ar_archive *raw = nullptr;
    if (ext == "zip")
        raw = ar_open_zip_archive(stream.get(), false);
    else if (ext == "7z")
        raw = ar_open_7z_archive(stream.get());
    else if (ext == "rar" || ext == "rsn")
        raw = ar_open_rar_archive(stream.get());
    std::unique_ptr<ar_archive, decltype(&ar_close_archive)> reader(raw, ar_close_archive);
    if (!reader)
        throw std::runtime_error("Unable to open archive.");
    size_t declared = 0, count = 0;
    while (ar_parse_entry(reader.get()))
    {
        const auto size = ar_entry_get_size(reader.get());
        if (++count > 16384 || size > member_limit || size > max_total - declared)
            throw std::runtime_error("Archive exceeds the supported entry or size limit.");
        declared += size;
    }
    if (!ar_at_eof(reader.get()))
        throw std::runtime_error("Corrupt or unsupported archive.");
    if (!count)
        return {};
    if (!ar_parse_entry_at(reader.get(), 0))
        throw std::runtime_error("Unable to rewind archive.");
    std::vector<archive_member> entries;
    size_t total = 0;
    do
    {
        if (entries.size() >= 16384)
            throw std::runtime_error("Archive contains too many entries.");
        const char *path = ar_entry_get_name(reader.get());
        if (!path)
            throw std::runtime_error("Unable to read archive entry name.");
        const size_t size = ar_entry_get_size(reader.get());
        if (size > member_limit || size > max_total - total)
            throw std::runtime_error("Archive decompressed data exceeds the supported size limit.");
        total += size;
        archive_member member{path, std::vector<uint8_t>(size)};
        std::array<uint8_t, 1> empty{};
        size_t offset = 0;
        do
        {
            const size_t amount = std::min<size_t>(size - offset, 32768);
            if (!ar_entry_uncompress(reader.get(), size ? member.bytes.data() + offset : empty.data(), amount))
                throw std::runtime_error("Corrupt, encrypted or unsupported archive entry.");
            offset += amount;
        } while (offset < size);
        entries.push_back(std::move(member));
    } while (ar_parse_entry(reader.get()));
    if (!ar_at_eof(reader.get()))
        throw std::runtime_error("Corrupt or unsupported archive.");
    return entries;
}
