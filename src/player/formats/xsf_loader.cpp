#include "xsf_loader.h"
#include <set>
#include <sstream>
#include <cmath>

namespace
{
    constexpr size_t limit = 256 * 1024 * 1024;
    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                       { return std::tolower(c); });
        return s;
    }
    struct loader
    {
        xsf_file result;
        std::set<std::filesystem::path> active;
        size_t total = 0, files = 0;
        void read(const std::filesystem::path &path, unsigned depth)
        {
            if (depth > 32 || ++files > 256)
                throw std::runtime_error("Too many xSF library dependencies.");
            auto canonical = std::filesystem::weakly_canonical(path);
            if (!active.insert(canonical).second)
                throw std::runtime_error("Cyclic xSF library dependency.");
            auto u8 = path.u8string();
            auto b = read_game_music(reinterpret_cast<const char *>(u8.c_str()));
            if (b.size() < 16 || std::memcmp(b.data(), "PSF", 3))
                throw std::runtime_error("Invalid xSF file or missing library: " + path.filename().string());
            if (!depth)
                result.version = b[3];
            if (b[3] != result.version)
                throw std::runtime_error("Mismatched xSF library version.");
            size_t reserved = replay_le32(b.data() + 4), packed = replay_le32(b.data() + 8);
            if (reserved > b.size() - 16 || packed > b.size() - 16 - reserved)
                throw std::runtime_error("Truncated xSF file.");
            if (crc32(0, b.data() + 16 + reserved, uInt(packed)) != replay_le32(b.data() + 12))
                throw std::runtime_error("xSF checksum mismatch.");
            const size_t tags_at = 16 + reserved + packed;
            std::map<std::string, std::string> tags;
            if (b.size() - tags_at >= 5 && !std::memcmp(b.data() + tags_at, "[TAG]", 5))
            {
                std::istringstream lines(std::string(reinterpret_cast<const char *>(b.data() + tags_at + 5), b.size() - tags_at - 5));
                std::string line;
                while (std::getline(lines, line))
                {
                    auto equal = line.find('=');
                    if (equal == std::string::npos)
                        continue;
                    auto key = lower(line.substr(0, equal)), value = line.substr(equal + 1);
                    auto trim = [](std::string &s)
                    { while (!s.empty() && static_cast<unsigned char>(s.back()) <= 32) s.pop_back(); size_t n=0; while (n<s.size() && static_cast<unsigned char>(s[n]) <= 32) ++n; s.erase(0,n); };
                    trim(key);
                    trim(value);
                    tags[key] = value;
                }
            }
            auto dependency = [&](const std::string &name)
            {
                std::string normalized = name;
                std::replace(normalized.begin(), normalized.end(), '\\', '/');
                auto lib = std::filesystem::path(reinterpret_cast<const char8_t *>(normalized.c_str()));
                if (lib.empty() || lib.is_absolute() || lib.has_root_name() || normalized.find(':') != std::string::npos)
                    throw std::runtime_error("Invalid xSF library path.");
                read(path.parent_path() / lib, depth + 1);
            };
            if (tags.count("_lib"))
                dependency(tags.at("_lib"));
            xsf_section section;
            section.reserved.assign(b.begin() + 16, b.begin() + 16 + reserved);
            if (packed)
                section.exe = inflate_xsf(b.data() + 16 + reserved, packed);
            size_t size = section.exe.size() + reserved;
            if (size > limit - total)
                throw std::runtime_error("xSF dependency data exceeds 256 MiB.");
            total += size;
            result.sections.push_back(std::move(section));
            for (unsigned i = 2; i <= 256; ++i)
            {
                auto it = tags.find("_lib" + std::to_string(i));
                if (it == tags.end())
                    break;
                dependency(it->second);
            }
            if (!depth)
                result.tags = std::move(tags);
            active.erase(canonical);
        }
    };
    unsigned time_ms(const std::string &text)
    {
        double seconds = 0;
        std::istringstream stream(text);
        std::string part;
        try
        {
            while (std::getline(stream, part, ':'))
            {
                size_t n = 0;
                double v = std::stod(part, &n);
                if (n != part.size() || !std::isfinite(v) || v < 0)
                    return 0;
                seconds = seconds * 60 + v;
            }
        }
        catch (...)
        {
            return 0;
        }
        return seconds > 0 && seconds <= 86400 ? unsigned(seconds * 1000) : 0;
    }
}
std::vector<uint8_t> inflate_xsf(const uint8_t *bytes, size_t size)
{
    z_stream z{};
    if (size > limit || inflateInit(&z) != Z_OK)
        throw std::runtime_error("Invalid xSF compressed data.");
    struct cleanup
    {
        z_stream *p;
        ~cleanup() { inflateEnd(p); }
    } guard{&z};
    z.next_in = const_cast<Bytef *>(bytes);
    z.avail_in = uInt(size);
    std::vector<uint8_t> out;
    std::array<uint8_t, 32768> block;
    int status;
    do
    {
        z.next_out = block.data();
        z.avail_out = uInt(block.size());
        status = inflate(&z, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END)
            throw std::runtime_error("Invalid xSF compressed stream.");
        size_t n = block.size() - z.avail_out;
        if (n > limit - out.size())
            throw std::runtime_error("xSF decompression limit exceeded.");
        out.insert(out.end(), block.begin(), block.begin() + n);
    } while (status != Z_STREAM_END);
    if (z.avail_in)
        throw std::runtime_error("Trailing xSF compressed data.");
    return out;
}
xsf_file load_xsf(const char *path)
{
    loader l;
    l.read(std::filesystem::path(reinterpret_cast<const char8_t *>(path)), 0);
    return std::move(l.result);
}
bool xsf_map(std::vector<uint8_t> &out, const uint8_t *data, size_t size, size_t max, bool power_of_two)
{
    if (size < 8)
        return false;
    size_t offset = replay_le32(data), count = replay_le32(data + 4);
    if (count > size - 8 || offset > max || count > max - offset)
        return false;
    size_t end = offset + count;
    if (power_of_two)
    {
        size_t n = 1;
        while (n < end)
            n *= 2;
        end = n;
    }
    if (end > max)
        return false;
    if (out.size() < end)
        out.resize(end);
    std::copy_n(data + 8, count, out.data() + offset);
    return true;
}
bool xsf_engine::load(const char *path)
{
    file = load_xsf(path);
    if (!accepts(file.version))
        return false;
    title = file.tags.count("title") ? file.tags.at("title") : "";
    artist = file.tags.count("artist") ? file.tags.at("artist") : "";
    album = file.tags.count("game") ? file.tags.at("game") : "";
    duration = 180000;
    if (file.tags.count("length"))
    {
        auto ms = time_ms(file.tags.at("length"));
        if (ms)
            duration = ms;
    }
    return true;
}
int xsf_engine::tag_number(const char *name, int fallback) const
{
    auto it = file.tags.find(name);
    if (it == file.tags.end())
        return fallback;
    try
    {
        return std::stoi(it->second);
    }
    catch (...)
    {
        return fallback;
    }
}
auddecode *create_psx() { return new replay_decoder(make_psx_engine(), {"psf", "minipsf", "psf2", "minipsf2"}); }
auddecode *create_sega() { return new replay_decoder(make_sega_engine(), {"ssf", "minissf", "dsf", "minidsf"}); }
auddecode *create_qsf() { return new replay_decoder(make_qsf_engine(), {"qsf", "miniqsf"}); }
auddecode *create_usf() { return new replay_decoder(make_usf_engine(), {"usf", "miniusf"}); }
auddecode *create_gsf() { return new replay_decoder(make_gsf_engine(), {"gsf", "minigsf"}); }
auddecode *create_snsf() { return new replay_decoder(make_snsf_engine(), {"snsf", "minisnsf"}); }
auddecode *create_2sf() { return new replay_decoder(make_2sf_engine(), {"2sf", "mini2sf"}); }
