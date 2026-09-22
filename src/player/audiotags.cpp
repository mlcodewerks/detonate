#include "audiotags.hpp"
#include "formats/futurecomposer_format.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits>

namespace audiotags
{
    namespace
    {

        using u8 = std::uint8_t;
        using u16 = std::uint16_t;
        using u32 = std::uint32_t;
        using u64 = std::uint64_t;

        static bool range_ok(std::size_t pos, std::size_t need, std::size_t size)
        {
            return pos <= size && need <= size - pos;
        }

        static u16 le16(const u8 *p)
        {
            return static_cast<u16>(p[0] | (static_cast<u16>(p[1]) << 8));
        }

        static u32 le32(const u8 *p)
        {
            return static_cast<u32>(p[0]) |
                   (static_cast<u32>(p[1]) << 8) |
                   (static_cast<u32>(p[2]) << 16) |
                   (static_cast<u32>(p[3]) << 24);
        }

        static u64 le64(const u8 *p)
        {
            return static_cast<u64>(le32(p)) | (static_cast<u64>(le32(p + 4)) << 32);
        }

        static u16 be16(const u8 *p)
        {
            return static_cast<u16>((static_cast<u16>(p[0]) << 8) | p[1]);
        }

        static u32 be24(const u8 *p)
        {
            return (static_cast<u32>(p[0]) << 16) |
                   (static_cast<u32>(p[1]) << 8) |
                   static_cast<u32>(p[2]);
        }

        static u32 be32(const u8 *p)
        {
            return (static_cast<u32>(p[0]) << 24) |
                   (static_cast<u32>(p[1]) << 16) |
                   (static_cast<u32>(p[2]) << 8) |
                   static_cast<u32>(p[3]);
        }

        static u64 be64(const u8 *p)
        {
            return (static_cast<u64>(be32(p)) << 32) | be32(p + 4);
        }

        static u32 synchsafe32(const u8 *p)
        {
            return (static_cast<u32>(p[0] & 0x7f) << 21) |
                   (static_cast<u32>(p[1] & 0x7f) << 14) |
                   (static_cast<u32>(p[2] & 0x7f) << 7) |
                   static_cast<u32>(p[3] & 0x7f);
        }

        static std::string upper_ascii(std::string s)
        {
            for (char &c : s)
            {
                const unsigned char ch = static_cast<unsigned char>(c);
                if (ch >= 'a' && ch <= 'z')
                    c = static_cast<char>(ch - 'a' + 'A');
            }
            return s;
        }

        static std::string lower_ascii(std::string s)
        {
            for (char &c : s)
            {
                const unsigned char ch = static_cast<unsigned char>(c);
                if (ch >= 'A' && ch <= 'Z')
                    c = static_cast<char>(ch - 'A' + 'a');
            }
            return s;
        }

        static std::string trim_ascii(std::string s)
        {
            std::size_t begin = 0;
            while (begin < s.size())
            {
                const unsigned char c = static_cast<unsigned char>(s[begin]);
                if (c > 0x20 && c != 0x7f)
                    break;
                ++begin;
            }
            std::size_t end = s.size();
            while (end > begin)
            {
                const unsigned char c = static_cast<unsigned char>(s[end - 1]);
                if (c > 0x20 && c != 0x7f)
                    break;
                --end;
            }
            return s.substr(begin, end - begin);
        }

        static void set_error(std::string *error, const char *text)
        {
            if (error)
                *error = text ? text : "";
        }

        static void append_utf8(std::string &out, u32 cp)
        {
            if (cp <= 0x7f)
            {
                out.push_back(static_cast<char>(cp));
            }
            else if (cp <= 0x7ff)
            {
                out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            }
            else if (cp <= 0xffff)
            {
                if (cp >= 0xd800 && cp <= 0xdfff)
                    cp = 0xfffd;
                out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            }
            else if (cp <= 0x10ffff)
            {
                out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            }
            else
            {
                append_utf8(out, 0xfffd);
            }
        }

        static std::string latin1_to_utf8(const u8 *p, std::size_t n)
        {
            std::string out;
            out.reserve(n + n / 2);
            for (std::size_t i = 0; i < n; ++i)
            {
                const u8 c = p[i];
                if (c == 0)
                    continue;
                if (c < 0x80)
                {
                    out.push_back(static_cast<char>(c));
                }
                else
                {
                    out.push_back(static_cast<char>(0xc0 | (c >> 6)));
                    out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
                }
            }
            return trim_ascii(out);
        }

        static std::string utf16_to_utf8(const u8 *p, std::size_t n, bool big_endian, bool bom_allowed)
        {
            if (n < 2)
                return {};
            std::size_t pos = 0;
            if (bom_allowed && n >= 2)
            {
                if (p[0] == 0xff && p[1] == 0xfe)
                {
                    big_endian = false;
                    pos = 2;
                }
                else if (p[0] == 0xfe && p[1] == 0xff)
                {
                    big_endian = true;
                    pos = 2;
                }
            }
            std::string out;
            out.reserve(n);
            auto get16 = [&](const u8 *q) -> u16
            {
                return big_endian ? be16(q) : le16(q);
            };
            while (pos + 1 < n)
            {
                u16 w1 = get16(p + pos);
                pos += 2;
                if (w1 == 0)
                    continue;
                u32 cp = w1;
                if (w1 >= 0xd800 && w1 <= 0xdbff)
                {
                    if (pos + 1 < n)
                    {
                        const u16 w2 = get16(p + pos);
                        if (w2 >= 0xdc00 && w2 <= 0xdfff)
                        {
                            pos += 2;
                            cp = 0x10000u + ((static_cast<u32>(w1 - 0xd800) << 10) |
                                             static_cast<u32>(w2 - 0xdc00));
                        }
                        else
                        {
                            cp = 0xfffd;
                        }
                    }
                    else
                    {
                        cp = 0xfffd;
                    }
                }
                else if (w1 >= 0xdc00 && w1 <= 0xdfff)
                {
                    cp = 0xfffd;
                }
                append_utf8(out, cp);
            }
            return trim_ascii(out);
        }

        static std::string id3_decode_text_bytes(u8 encoding, const u8 *p, std::size_t n)
        {
            while (n && p[n - 1] == 0)
                --n;
            switch (encoding)
            {
            case 0:
                return latin1_to_utf8(p, n);
            case 1:
                return utf16_to_utf8(p, n, false, true);
            case 2:
                return utf16_to_utf8(p, n, true, false);
            case 3:
                return trim_ascii(std::string(reinterpret_cast<const char *>(p), n));
            default:
                return {};
            }
        }

        static std::size_t encoded_terminator(const u8 *p, std::size_t n, u8 encoding)
        {
            if (encoding == 0 || encoding == 3)
            {
                for (std::size_t i = 0; i < n; ++i)
                {
                    if (p[i] == 0)
                        return i + 1;
                }
                return n;
            }
            for (std::size_t i = 0; i + 1 < n; i += 2)
            {
                if (p[i] == 0 && p[i + 1] == 0)
                    return i + 2;
            }
            return n;
        }

        static std::string id3_decode_text_frame(const u8 *p, std::size_t n)
        {
            if (!n || p[0] > 3)
                return {};
            const u8 enc = p[0];
            p++;
            --n;

            std::string out;
            std::size_t pos = 0;
            while (pos < n)
            {
                const std::size_t consumed = encoded_terminator(p + pos, n - pos, enc);
                std::size_t text_len = consumed;
                if (enc == 0 || enc == 3)
                {
                    if (text_len && p[pos + text_len - 1] == 0)
                        --text_len;
                }
                else
                {
                    if (text_len >= 2 && p[pos + text_len - 2] == 0 && p[pos + text_len - 1] == 0)
                        text_len -= 2;
                }
                std::string part = id3_decode_text_bytes(enc, p + pos, text_len);
                if (!part.empty())
                {
                    if (!out.empty())
                        out += " / ";
                    out += part;
                }
                if (consumed == 0 || consumed >= n - pos)
                    break;
                pos += consumed;
            }
            return trim_ascii(out);
        }

        static std::vector<u8> deunsync(const u8 *p, std::size_t n)
        {
            std::vector<u8> out;
            out.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
            {
                out.push_back(p[i]);
                if (p[i] == 0xff && i + 1 < n && p[i + 1] == 0x00)
                    ++i;
            }
            return out;
        }

        static void add_tag(Metadata &m, const std::string &key, const std::string &value)
        {
            if (value.empty() || key.empty())
                return;
            m.tags[upper_ascii(key)].push_back(value);
        }

        static u32 parse_u32_prefix(const std::string &s)
        {
            u64 v = 0;
            bool any = false;
            for (char ch : s)
            {
                if (ch < '0' || ch > '9')
                    break;
                any = true;
                v = v * 10 + static_cast<unsigned>(ch - '0');
                if (v > std::numeric_limits<u32>::max())
                    return std::numeric_limits<u32>::max();
            }
            return any ? static_cast<u32>(v) : 0;
        }

        static void parse_pair(const std::string &s, u32 &a, u32 &b, bool overwrite)
        {
            const std::size_t slash = s.find('/');
            const u32 first = parse_u32_prefix(trim_ascii(s.substr(0, slash)));
            const u32 second = slash == std::string::npos ? 0 : parse_u32_prefix(trim_ascii(s.substr(slash + 1)));
            if (first && (overwrite || !a))
                a = first;
            if (second && (overwrite || !b))
                b = second;
        }

        static void set_string(std::string &dst, const std::string &src, bool overwrite)
        {
            if (!src.empty() && (overwrite || dst.empty()))
                dst = src;
        }

        static void apply_common(Metadata &m, const std::string &key_in, const std::string &value, bool overwrite = false)
        {
            if (value.empty())
                return;
            const std::string key = upper_ascii(key_in);
            const auto it = m.tags.find(key);
            if (it == m.tags.end() || it->second.empty() || it->second.back() != value)
                add_tag(m, key, value);

            if (key == "TITLE")
                set_string(m.title, value, overwrite);
            else if (key == "ARTIST" || key == "PERFORMER")
                set_string(m.artist, value, overwrite);
            else if (key == "ALBUM")
                set_string(m.album, value, overwrite);
            else if (key == "ALBUMARTIST" || key == "ALBUM ARTIST" || key == "ALBUM_ARTIST" || key == "ENSEMBLE")
                set_string(m.album_artist, value, overwrite);
            else if (key == "COMPOSER")
                set_string(m.composer, value, overwrite);
            else if (key == "GENRE")
                set_string(m.genre, value, overwrite);
            else if (key == "DATE" || key == "YEAR")
                set_string(m.date, value, overwrite);
            else if (key == "COMMENT" || key == "DESCRIPTION")
                set_string(m.comment, value, overwrite);
            else if (key == "TRACKNUMBER" || key == "TRACK")
                parse_pair(value, m.track_number, m.track_total, overwrite);
            else if (key == "TRACKTOTAL" || key == "TOTALTRACKS")
            {
                const u32 v = parse_u32_prefix(value);
                if (v && (overwrite || !m.track_total))
                    m.track_total = v;
            }
            else if (key == "DISCNUMBER" || key == "DISC")
                parse_pair(value, m.disc_number, m.disc_total, overwrite);
            else if (key == "DISCTOTAL" || key == "TOTALDISCS")
            {
                const u32 v = parse_u32_prefix(value);
                if (v && (overwrite || !m.disc_total))
                    m.disc_total = v;
            }
        }

        static std::string cp437_to_utf8(const u8 *p, std::size_t n, bool multiline = false)
        {
            static const u16 ext[128] = {
                0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
                0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
                0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
                0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
                0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
                0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
                0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
                0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
                0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
                0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
                0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
                0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
                0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
                0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
                0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
                0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};
            std::string out;
            out.reserve(n + n / 4);
            bool previous_cr = false;
            for (std::size_t i = 0; i < n; ++i)
            {
                const u8 c = p[i];
                if (c == 0)
                    break;
                if (multiline && c == '\r')
                {
                    out.push_back('\n');
                    previous_cr = true;
                    continue;
                }
                if (multiline && c == '\n')
                {
                    if (!previous_cr)
                        out.push_back('\n');
                    previous_cr = false;
                    continue;
                }
                previous_cr = false;
                if (c == '\t' && multiline)
                {
                    out.push_back('\t');
                }
                else if (c < 0x20 || c == 0x7f)
                {
                    if (!out.empty() && out.back() != ' ' && out.back() != '\n')
                        out.push_back(' ');
                }
                else if (c < 0x80)
                {
                    out.push_back(static_cast<char>(c));
                }
                else
                {
                    append_utf8(out, ext[c - 0x80]);
                }
            }
            if (!multiline)
                return trim_ascii(out);
            while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\n'))
                out.pop_back();
            std::size_t begin = 0;
            while (begin < out.size() && (out[begin] == ' ' || out[begin] == '\t' || out[begin] == '\n'))
                ++begin;
            return out.substr(begin);
        }

        static std::string latin1_fixed(const u8 *p, std::size_t n)
        {
            std::size_t len = 0;
            while (len < n && p[len])
                ++len;
            return latin1_to_utf8(p, len);
        }

        static std::string cp437_fixed(const u8 *p, std::size_t n)
        {
            std::size_t len = 0;
            while (len < n && p[len])
                ++len;
            return cp437_to_utf8(p, len, false);
        }

        static std::string decimal_string(u32 v)
        {
            return std::to_string(static_cast<unsigned long long>(v));
        }

        static std::string hex16_string(u16 v)
        {
            static const char hex[] = "0123456789ABCDEF";
            std::string s("0x0000");
            s[2] = hex[(v >> 12) & 15];
            s[3] = hex[(v >> 8) & 15];
            s[4] = hex[(v >> 4) & 15];
            s[5] = hex[v & 15];
            return s;
        }

        static std::string tracker_version_string(u16 v)
        {
            const unsigned major = (v >> 8) & 0xff;
            const unsigned minor = v & 0xff;
            std::string s = decimal_string(major);
            s += '.';
            const unsigned tens = (minor >> 4) & 0xf;
            const unsigned ones = minor & 0xf;
            s.push_back(static_cast<char>('0' + (tens <= 9 ? tens : 0)));
            s.push_back(static_cast<char>('0' + (ones <= 9 ? ones : 0)));
            return s;
        }

        static bool add_size(std::size_t a, std::size_t b, std::size_t &out)
        {
            if (b > std::numeric_limits<std::size_t>::max() - a)
                return false;
            out = a + b;
            return true;
        }

        static int mod_signature_channels(const u8 *p)
        {
            const char a = static_cast<char>(p[0]);
            const char b = static_cast<char>(p[1]);
            const char c = static_cast<char>(p[2]);
            const char d = static_cast<char>(p[3]);
            const std::string sig(reinterpret_cast<const char *>(p), 4);
            if (sig == "M.K." || sig == "M!K!" || sig == "M&K!" || sig == "N.T." ||
                sig == "FLT4" || sig == "EXO4")
                return 4;
            if (sig == "FLT8" || sig == "EXO8" || sig == "CD81" || sig == "OKTA" || sig == "OCTA")
                return 8;
            if (a >= '1' && a <= '9' && b == 'C' && c == 'H' && d == 'N')
                return a - '0';
            if (a >= '1' && a <= '9' && b >= '0' && b <= '9' && c == 'C' && (d == 'H' || d == 'N'))
                return (a - '0') * 10 + (b - '0');
            if (a == 'T' && b == 'D' && c == 'Z' && d >= '1' && d <= '9')
                return d - '0';
            if (a == 'F' && b == 'A' && c >= '0' && c <= '9' && d >= '0' && d <= '9')
            {
                const int n = (c - '0') * 10 + (d - '0');
                if (n >= 1 && n <= 99)
                    return n;
            }
            return 0;
        }

        static bool validate_mod_layout(const u8 *data,
                                        std::size_t size,
                                        unsigned sample_count,
                                        std::size_t song_len_off,
                                        std::size_t orders_off,
                                        std::size_t pattern_off,
                                        unsigned channels)
        {
            if (!channels || channels > 99 || !range_ok(orders_off, 128, size) || song_len_off >= size)
                return false;
            const u8 song_len = data[song_len_off];
            if (!song_len || song_len > 128)
                return false;
            u8 max_pattern = 0;
            for (unsigned i = 0; i < song_len; ++i)
                max_pattern = std::max(max_pattern, data[orders_off + i]);
            if (max_pattern > 127)
                return false;

            u64 sample_bytes = 0;
            for (unsigned i = 0; i < sample_count; ++i)
            {
                const std::size_t h = 20 + static_cast<std::size_t>(i) * 30;
                if (!range_ok(h, 30, size))
                    return false;
                if ((data[h + 24] & 0xf0) != 0 || data[h + 25] > 64)
                    return false;
                sample_bytes += static_cast<u64>(be16(data + h + 22)) * 2u;
            }
            const u64 pattern_bytes = static_cast<u64>(max_pattern + 1u) * 64u * channels * 4u;
            const u64 required = static_cast<u64>(pattern_off) + pattern_bytes + sample_bytes;
            return required <= size;
        }

        static bool parse_mod(const u8 *data, std::size_t size, std::size_t start, Metadata &m)
        {
            if (start != 0 || size < 600)
                return false;
            unsigned sample_count = 0;
            unsigned channels = 0;
            std::size_t song_len_off = 0;
            std::size_t orders_off = 0;
            std::size_t pattern_off = 0;
            std::string signature;

            if (size >= 1084)
            {
                channels = static_cast<unsigned>(mod_signature_channels(data + 1080));
                if (channels)
                {
                    sample_count = 31;
                    song_len_off = 950;
                    orders_off = 952;
                    pattern_off = 1084;
                    signature.assign(reinterpret_cast<const char *>(data + 1080), 4);
                }
            }
            if (!sample_count)
            {
                sample_count = 15;
                channels = 4;
                song_len_off = 470;
                orders_off = 472;
                pattern_off = 600;
                if (!validate_mod_layout(data, size, sample_count, song_len_off, orders_off, pattern_off, channels))
                    return false;
                signature = "15-sample";
            }
            else
            {
                if (!range_ok(orders_off, 128, size))
                    return false;
                const u8 song_len = data[song_len_off];
                if (!song_len || song_len > 128)
                    return false;
                for (unsigned i = 0; i < sample_count; ++i)
                {
                    const std::size_t h = 20 + static_cast<std::size_t>(i) * 30;
                    if (!range_ok(h, 30, size) || (data[h + 24] & 0xf0) != 0 || data[h + 25] > 64)
                        return false;
                }
            }

            m.format = Format::mod;
            const std::string title = latin1_fixed(data, 20);
            if (!title.empty())
                apply_common(m, "TITLE", title, true);
            add_tag(m, "MODULE:FORMAT", "MOD");
            add_tag(m, "MODULE:SIGNATURE", signature);
            add_tag(m, "MODULE:CHANNELS", decimal_string(channels));
            add_tag(m, "MODULE:SAMPLES", decimal_string(sample_count));
            add_tag(m, "MODULE:ORDERS", decimal_string(data[song_len_off]));

            std::string message;
            for (unsigned i = 0; i < sample_count; ++i)
            {
                const std::size_t h = 20 + static_cast<std::size_t>(i) * 30;
                const std::string name = latin1_fixed(data + h, 22);
                if (name.empty())
                    continue;
                add_tag(m, "MODULE:SAMPLE_NAME", name);
                if (name.size() > 1 && name[0] == '#')
                {
                    const std::string line = trim_ascii(name.substr(1));
                    if (!line.empty())
                    {
                        if (!message.empty())
                            message += '\n';
                        message += line;
                    }
                }
            }
            if (!message.empty())
            {
                add_tag(m, "MODULE:MESSAGE", message);
                set_string(m.comment, message, false);
            }
            return true;
        }

        static bool parse_s3m(const u8 *data, std::size_t size, std::size_t start, Metadata &m)
        {
            if (start != 0 || size < 96 || data[28] != 0x1a || data[29] != 0x10 || std::memcmp(data + 44, "SCRM", 4) != 0)
                return false;
            const u16 orders = le16(data + 32);
            const u16 instruments = le16(data + 34);
            const u16 patterns = le16(data + 36);
            if (orders > 4096 || instruments > 4096 || patterns > 4096)
                return false;
            std::size_t table_end = 96;
            if (!add_size(table_end, orders, table_end) ||
                !add_size(table_end, static_cast<std::size_t>(instruments) * 2u, table_end) ||
                !add_size(table_end, static_cast<std::size_t>(patterns) * 2u, table_end) || table_end > size)
                return false;

            m.format = Format::s3m;
            const std::string title = cp437_fixed(data, 28);
            if (!title.empty())
                apply_common(m, "TITLE", title, true);
            add_tag(m, "MODULE:FORMAT", "S3M");
            add_tag(m, "MODULE:ORDERS", decimal_string(orders));
            add_tag(m, "MODULE:INSTRUMENTS", decimal_string(instruments));
            add_tag(m, "MODULE:PATTERNS", decimal_string(patterns));
            add_tag(m, "MODULE:CREATED_WITH", hex16_string(le16(data + 40)));
            add_tag(m, "MODULE:INITIAL_SPEED", decimal_string(data[49]));
            add_tag(m, "MODULE:INITIAL_TEMPO", decimal_string(data[50]));
            unsigned active_channels = 0;
            for (unsigned i = 0; i < 32; ++i)
                if (data[64 + i] < 32)
                    ++active_channels;
            add_tag(m, "MODULE:CHANNELS", decimal_string(active_channels));

            const std::size_t instrument_table = 96 + orders;
            for (u16 i = 0; i < instruments; ++i)
            {
                const std::size_t off = static_cast<std::size_t>(le16(data + instrument_table + static_cast<std::size_t>(i) * 2u)) << 4;
                if (!off || !range_ok(off, 80, size))
                    continue;
                const std::string name = cp437_fixed(data + off + 48, 28);
                if (!name.empty())
                    add_tag(m, "MODULE:INSTRUMENT_NAME", name);
            }
            return true;
        }

        static void parse_xm_names(const u8 *data,
                                   std::size_t size,
                                   std::size_t pos,
                                   u16 patterns,
                                   u16 instruments,
                                   Metadata &m)
        {
            for (u16 i = 0; i < patterns; ++i)
            {
                if (!range_ok(pos, 9, size))
                    return;
                const u32 header_size = le32(data + pos);
                if (header_size < 9 || !range_ok(pos, header_size, size))
                    return;
                const u16 packed_size = le16(data + pos + 7);
                std::size_t next = 0;
                if (!add_size(pos, header_size, next) || !add_size(next, packed_size, next) || next > size)
                    return;
                pos = next;
            }

            for (u16 i = 0; i < instruments; ++i)
            {
                if (!range_ok(pos, 29, size))
                    return;
                const u32 instrument_size = le32(data + pos);
                if (instrument_size < 29 || !range_ok(pos, instrument_size, size))
                    return;
                const std::string instrument_name = cp437_fixed(data + pos + 4, 22);
                if (!instrument_name.empty())
                    add_tag(m, "MODULE:INSTRUMENT_NAME", instrument_name);
                const u16 samples = le16(data + pos + 27);
                if (!samples)
                {
                    pos += instrument_size;
                    continue;
                }
                if (instrument_size < 33)
                    return;
                const u32 sample_header_size = le32(data + pos + 29);
                if (sample_header_size < 40)
                    return;
                const std::size_t sample_headers = pos + instrument_size;
                u64 sample_bytes = 0;
                for (u16 s = 0; s < samples; ++s)
                {
                    const std::size_t sh = sample_headers + static_cast<std::size_t>(s) * sample_header_size;
                    if (!range_ok(sh, sample_header_size, size))
                        return;
                    sample_bytes += le32(data + sh);
                    if (sample_bytes > size)
                        return;
                    const std::string sample_name = cp437_fixed(data + sh + 18, 22);
                    if (!sample_name.empty())
                        add_tag(m, "MODULE:SAMPLE_NAME", sample_name);
                }
                std::size_t data_pos = 0;
                if (!add_size(sample_headers, static_cast<std::size_t>(samples) * sample_header_size, data_pos))
                    return;
                if (sample_bytes > std::numeric_limits<std::size_t>::max() - data_pos)
                    return;
                data_pos += static_cast<std::size_t>(sample_bytes);
                if (data_pos > size)
                    return;
                pos = data_pos;
            }
        }

        static bool parse_xm(const u8 *data, std::size_t size, std::size_t start, Metadata &m)
        {
            static const char magic[] = "Extended Module: ";
            if (start != 0 || size < 80 || std::memcmp(data, magic, 17) != 0 || data[37] != 0x1a)
                return false;
            const u16 version = le16(data + 58);
            const u32 header_size = le32(data + 60);
            if (header_size < 20 || static_cast<u64>(60) + header_size > size)
                return false;
            const u16 song_length = le16(data + 64);
            const u16 channels = le16(data + 68);
            const u16 patterns = le16(data + 70);
            const u16 instruments = le16(data + 72);
            if (song_length > 256 || channels > 256 || patterns > 4096 || instruments > 4096)
                return false;

            m.format = Format::xm;
            const std::string title = cp437_fixed(data + 17, 20);
            const std::string tracker = cp437_fixed(data + 38, 20);
            if (!title.empty())
                apply_common(m, "TITLE", title, true);
            add_tag(m, "MODULE:FORMAT", "XM");
            if (!tracker.empty())
                add_tag(m, "MODULE:TRACKER", tracker);
            add_tag(m, "MODULE:VERSION", tracker_version_string(version));
            add_tag(m, "MODULE:ORDERS", decimal_string(song_length));
            add_tag(m, "MODULE:CHANNELS", decimal_string(channels));
            add_tag(m, "MODULE:PATTERNS", decimal_string(patterns));
            add_tag(m, "MODULE:INSTRUMENTS", decimal_string(instruments));
            add_tag(m, "MODULE:INITIAL_SPEED", decimal_string(le16(data + 76)));
            add_tag(m, "MODULE:INITIAL_TEMPO", decimal_string(le16(data + 78)));
            add_tag(m, "MODULE:FREQUENCY_TABLE", (le16(data + 74) & 1) ? "linear" : "amiga");

            // XM 1.04 stores all patterns before instruments. Older XM revisions may
            // use different block ordering, so keep header metadata but do not guess.
            if (version >= 0x0104)
                parse_xm_names(data, size, 60 + header_size, patterns, instruments, m);
            return true;
        }

        static bool parse_it(const u8 *data, std::size_t size, std::size_t start, Metadata &m)
        {
            if (start != 0 || size < 192 || std::memcmp(data, "IMPM", 4) != 0)
                return false;
            const u16 orders = le16(data + 32);
            const u16 instruments = le16(data + 34);
            const u16 samples = le16(data + 36);
            const u16 patterns = le16(data + 38);
            if (orders > 4096 || instruments > 4096 || samples > 4096 || patterns > 4096)
                return false;
            std::size_t table_end = 192;
            if (!add_size(table_end, orders, table_end) ||
                !add_size(table_end, static_cast<std::size_t>(instruments) * 4u, table_end) ||
                !add_size(table_end, static_cast<std::size_t>(samples) * 4u, table_end) ||
                !add_size(table_end, static_cast<std::size_t>(patterns) * 4u, table_end) || table_end > size)
                return false;

            m.format = Format::it;
            const std::string title = cp437_fixed(data + 4, 26);
            if (!title.empty())
                apply_common(m, "TITLE", title, true);
            add_tag(m, "MODULE:FORMAT", "IT");
            add_tag(m, "MODULE:ORDERS", decimal_string(orders));
            add_tag(m, "MODULE:INSTRUMENTS", decimal_string(instruments));
            add_tag(m, "MODULE:SAMPLES", decimal_string(samples));
            add_tag(m, "MODULE:PATTERNS", decimal_string(patterns));
            add_tag(m, "MODULE:CREATED_WITH", tracker_version_string(le16(data + 40)));
            add_tag(m, "MODULE:COMPATIBLE_WITH", tracker_version_string(le16(data + 42)));
            add_tag(m, "MODULE:INITIAL_SPEED", decimal_string(data[50]));
            add_tag(m, "MODULE:INITIAL_TEMPO", decimal_string(data[51]));
            unsigned active_channels = 0;
            for (unsigned i = 0; i < 64; ++i)
                if (data[64 + i] < 128)
                    ++active_channels;
            add_tag(m, "MODULE:CHANNELS", decimal_string(active_channels));

            const u16 message_length = le16(data + 54);
            const u32 message_offset = le32(data + 56);
            if (message_length && message_offset && range_ok(message_offset, message_length, size))
            {
                const std::string message = cp437_to_utf8(data + message_offset, message_length, true);
                if (!message.empty())
                {
                    add_tag(m, "MODULE:MESSAGE", message);
                    set_string(m.comment, message, false);
                }
            }

            const std::size_t instrument_table = 192 + orders;
            const std::size_t sample_table = instrument_table + static_cast<std::size_t>(instruments) * 4u;
            for (u16 i = 0; i < instruments; ++i)
            {
                const u32 off = le32(data + instrument_table + static_cast<std::size_t>(i) * 4u);
                if (!off || !range_ok(off, 58, size) || std::memcmp(data + off, "IMPI", 4) != 0)
                    continue;
                const std::string name = cp437_fixed(data + off + 32, 26);
                if (!name.empty())
                    add_tag(m, "MODULE:INSTRUMENT_NAME", name);
            }
            for (u16 i = 0; i < samples; ++i)
            {
                const u32 off = le32(data + sample_table + static_cast<std::size_t>(i) * 4u);
                if (!off || !range_ok(off, 80, size) || std::memcmp(data + off, "IMPS", 4) != 0)
                    continue;
                const std::string name = cp437_fixed(data + off + 20, 26);
                if (!name.empty())
                    add_tag(m, "MODULE:SAMPLE_NAME", name);
            }
            return true;
        }

        static const char *const kId3Genres[] = {
            "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop",
            "Jazz", "Metal", "New Age", "Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock",
            "Techno", "Industrial", "Alternative", "Ska", "Death Metal", "Pranks", "Soundtrack", "Euro-Techno",
            "Ambient", "Trip-Hop", "Vocal", "Jazz+Funk", "Fusion", "Trance", "Classical", "Instrumental",
            "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise", "Alternative Rock", "Bass", "Soul",
            "Punk", "Space", "Meditative", "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic",
            "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance", "Dream", "Southern Rock",
            "Comedy", "Cult", "Gangsta", "Top 40", "Christian Rap", "Pop/Funk", "Jungle", "Native American",
            "Cabaret", "New Wave", "Psychedelic", "Rave", "Showtunes", "Trailer", "Lo-Fi", "Tribal", "Acid Punk",
            "Acid Jazz", "Polka", "Retro", "Musical", "Rock & Roll", "Hard Rock", "Folk", "Folk-Rock",
            "National Folk", "Swing", "Fast Fusion", "Bebop", "Latin", "Revival", "Celtic", "Bluegrass",
            "Avantgarde", "Gothic Rock", "Progressive Rock", "Psychedelic Rock", "Symphonic Rock", "Slow Rock",
            "Big Band", "Chorus", "Easy Listening", "Acoustic", "Humour", "Speech", "Chanson", "Opera",
            "Chamber Music", "Sonata", "Symphony", "Booty Bass", "Primus", "Porn Groove", "Satire", "Slow Jam",
            "Club", "Tango", "Samba", "Folklore", "Ballad", "Power Ballad", "Rhythmic Soul", "Freestyle", "Duet",
            "Punk Rock", "Drum Solo", "A Cappella", "Euro-House", "Dance Hall", "Goa", "Drum & Bass", "Club-House",
            "Hardcore", "Terror", "Indie", "BritPop", "Negerpunk", "Polsk Punk", "Beat", "Christian Gangsta Rap",
            "Heavy Metal", "Black Metal", "Crossover", "Contemporary Christian", "Christian Rock", "Merengue", "Salsa",
            "Thrash Metal", "Anime", "JPop", "SynthPop"};

        static std::string id3_genre_name(u8 id)
        {
            if (id < sizeof(kId3Genres) / sizeof(kId3Genres[0]))
                return kId3Genres[id];
            return {};
        }

        static std::string mime_from_image(const u8 *p, std::size_t n)
        {
            if (n >= 8 && std::memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0)
                return "image/png";
            if (n >= 3 && p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff)
                return "image/jpeg";
            if (n >= 6 && (std::memcmp(p, "GIF87a", 6) == 0 || std::memcmp(p, "GIF89a", 6) == 0))
                return "image/gif";
            if (n >= 12 && std::memcmp(p, "RIFF", 4) == 0 && std::memcmp(p + 8, "WEBP", 4) == 0)
                return "image/webp";
            if (n >= 2 && p[0] == 'B' && p[1] == 'M')
                return "image/bmp";
            return "application/octet-stream";
        }

        static void fill_image_dimensions(Picture &pic)
        {
            const u8 *p = pic.data.data();
            const std::size_t n = pic.data.size();
            if (n >= 24 && std::memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0 && std::memcmp(p + 12, "IHDR", 4) == 0)
            {
                pic.width = be32(p + 16);
                pic.height = be32(p + 20);
                if (n >= 25)
                    pic.depth = p[24];
                return;
            }
            if (n >= 10 && (std::memcmp(p, "GIF87a", 6) == 0 || std::memcmp(p, "GIF89a", 6) == 0))
            {
                pic.width = le16(p + 6);
                pic.height = le16(p + 8);
                return;
            }
            if (n >= 24 && std::memcmp(p, "RIFF", 4) == 0 && std::memcmp(p + 8, "WEBP", 4) == 0)
            {
                if (std::memcmp(p + 12, "VP8X", 4) == 0 && n >= 30)
                {
                    pic.width = 1 + static_cast<u32>(p[24] | (p[25] << 8) | (p[26] << 16));
                    pic.height = 1 + static_cast<u32>(p[27] | (p[28] << 8) | (p[29] << 16));
                }
                return;
            }
            if (n >= 4 && p[0] == 0xff && p[1] == 0xd8)
            {
                std::size_t pos = 2;
                while (pos + 3 < n)
                {
                    if (p[pos] != 0xff)
                    {
                        ++pos;
                        continue;
                    }
                    while (pos < n && p[pos] == 0xff)
                        ++pos;
                    if (pos >= n)
                        break;
                    const u8 marker = p[pos++];
                    if (marker == 0xd8 || marker == 0xd9 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
                        continue;
                    if (pos + 2 > n)
                        break;
                    const u16 seg = be16(p + pos);
                    if (seg < 2 || pos + seg > n)
                        break;
                    const bool sof = (marker >= 0xc0 && marker <= 0xc3) ||
                                     (marker >= 0xc5 && marker <= 0xc7) ||
                                     (marker >= 0xc9 && marker <= 0xcb) ||
                                     (marker >= 0xcd && marker <= 0xcf);
                    if (sof && seg >= 8)
                    {
                        pic.depth = p[pos + 2];
                        pic.height = be16(p + pos + 3);
                        pic.width = be16(p + pos + 5);
                        return;
                    }
                    pos += seg;
                }
            }
        }

        static bool parse_flac_picture_block(const u8 *p, std::size_t n, Picture &pic)
        {
            std::size_t pos = 0;
            auto get32 = [&](u32 &v) -> bool
            {
                if (!range_ok(pos, 4, n))
                    return false;
                v = be32(p + pos);
                pos += 4;
                return true;
            };

            u32 mime_n = 0, desc_n = 0, data_n = 0;
            if (!get32(pic.type) || !get32(mime_n) || !range_ok(pos, mime_n, n))
                return false;
            pic.mime.assign(reinterpret_cast<const char *>(p + pos), mime_n);
            pos += mime_n;
            if (!get32(desc_n) || !range_ok(pos, desc_n, n))
                return false;
            pic.description.assign(reinterpret_cast<const char *>(p + pos), desc_n);
            pos += desc_n;
            if (!get32(pic.width) || !get32(pic.height) || !get32(pic.depth) || !get32(pic.colors) || !get32(data_n))
                return false;
            if (!range_ok(pos, data_n, n))
                return false;
            pic.data.assign(p + pos, p + pos + data_n);
            if (pic.mime.empty())
                pic.mime = mime_from_image(pic.data.data(), pic.data.size());
            return !pic.data.empty();
        }

        static std::vector<u8> base64_decode(const std::string &s)
        {
            static const signed char table[256] = {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
                52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -2, -1, -1,
                -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
                -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
                41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
            std::vector<u8> out;
            out.reserve((s.size() * 3) / 4 + 3);
            u32 val = 0;
            int bits = -8;
            for (unsigned char c : s)
            {
                if (c == '=')
                    break;
                const int d = table[c];
                if (d < 0)
                    continue;
                val = (val << 6) | d;
                bits += 6;
                if (bits >= 0)
                {
                    out.push_back(static_cast<u8>((val >> bits) & 0xff));
                    bits -= 8;
                }
            }
            return out;
        }

        static bool parse_apic(const u8 *p, std::size_t n, bool v22, Picture &pic)
        {
            if (n < (v22 ? 6u : 4u))
                return false;
            const u8 enc = p[0];
            if (enc > 3)
                return false;
            std::size_t pos = 1;

            if (v22)
            {
                if (!range_ok(pos, 3, n))
                    return false;
                const std::string fmt(reinterpret_cast<const char *>(p + pos), 3);
                pos += 3;
                if (fmt == "PNG")
                    pic.mime = "image/png";
                else if (fmt == "JPG")
                    pic.mime = "image/jpeg";
                else
                    pic.mime = "image/" + fmt;
            }
            else
            {
                const std::size_t mime_start = pos;
                while (pos < n && p[pos])
                    ++pos;
                if (pos >= n)
                    return false;
                pic.mime.assign(reinterpret_cast<const char *>(p + mime_start), pos - mime_start);
                ++pos;
            }

            if (pos >= n)
                return false;
            pic.type = p[pos++];
            if (pos > n)
                return false;
            const std::size_t term = encoded_terminator(p + pos, n - pos, enc);
            std::size_t desc_n = term;
            if (enc == 0 || enc == 3)
            {
                if (desc_n && p[pos + desc_n - 1] == 0)
                    --desc_n;
            }
            else if (desc_n >= 2 && p[pos + desc_n - 2] == 0 && p[pos + desc_n - 1] == 0)
            {
                desc_n -= 2;
            }
            pic.description = id3_decode_text_bytes(enc, p + pos, desc_n);
            pos += term;
            if (pos >= n)
                return false;
            pic.data.assign(p + pos, p + n);
            if (pic.mime.empty())
                pic.mime = mime_from_image(pic.data.data(), pic.data.size());
            fill_image_dimensions(pic);
            return !pic.data.empty();
        }

        static std::string parse_id3_comment(const u8 *p, std::size_t n)
        {
            if (n < 4 || p[0] > 3)
                return {};
            const u8 enc = p[0];
            std::size_t pos = 4; // encoding + 3-byte language
            if (pos > n)
                return {};
            const std::size_t term = encoded_terminator(p + pos, n - pos, enc);
            if (term > n - pos)
                return {};
            pos += term;
            if (pos > n)
                return {};
            return id3_decode_text_bytes(enc, p + pos, n - pos);
        }

        static void map_id3_text(Metadata &m, const std::string &id, const std::string &value)
        {
            if (value.empty())
                return;
            add_tag(m, "ID3:" + id, value);

            if (id == "TIT2" || id == "TT2")
                apply_common(m, "TITLE", value);
            else if (id == "TPE1" || id == "TP1")
                apply_common(m, "ARTIST", value);
            else if (id == "TALB" || id == "TAL")
                apply_common(m, "ALBUM", value);
            else if (id == "TPE2" || id == "TP2")
                apply_common(m, "ALBUMARTIST", value);
            else if (id == "TCOM" || id == "TCM")
                apply_common(m, "COMPOSER", value);
            else if (id == "TCON" || id == "TCO")
                apply_common(m, "GENRE", value);
            else if (id == "TDRC" || id == "TYER" || id == "TYE" || id == "TDOR")
                apply_common(m, "DATE", value);
            else if (id == "TRCK" || id == "TRK")
                apply_common(m, "TRACKNUMBER", value);
            else if (id == "TPOS" || id == "TPA")
                apply_common(m, "DISCNUMBER", value);
            else if (id == "TLEN" || id == "TLE")
            {
                const u32 ms = parse_u32_prefix(value);
                if (ms && m.duration_seconds <= 0.0)
                    m.duration_seconds = static_cast<double>(ms) / 1000.0;
            }
        }

        static bool valid_frame_id(const u8 *p, std::size_t n)
        {
            for (std::size_t i = 0; i < n; ++i)
            {
                const unsigned char c = p[i];
                if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                    return false;
            }
            return true;
        }

        static std::size_t parse_id3v2(const u8 *data, std::size_t size, Metadata &m)
        {
            if (size < 10 || std::memcmp(data, "ID3", 3) != 0)
                return 0;
            const u8 version = data[3];
            if (version < 2 || version > 4)
                return 0;
            if ((data[6] | data[7] | data[8] | data[9]) & 0x80)
                return 0;

            const u8 tag_flags = data[5];
            const std::size_t body_size = synchsafe32(data + 6);
            if (!range_ok(10, body_size, size))
                return 0;
            const std::size_t tag_end = 10 + body_size;
            std::size_t pos = 10;

            // ID3v2.2 allowed whole-tag compression. This reader intentionally skips
            // compressed tags rather than pulling in zlib, while still returning the
            // correct audio-data offset.
            if (version == 2 && (tag_flags & 0x40))
                return tag_end;

            if ((tag_flags & 0x40) && version >= 3)
            {
                if (!range_ok(pos, 4, tag_end))
                    return tag_end;
                if (version == 3)
                {
                    const u32 ext = be32(data + pos);
                    const std::size_t skip = 4ull + ext;
                    if (!range_ok(pos, skip, tag_end))
                        return tag_end;
                    pos += skip;
                }
                else
                {
                    const u32 ext = synchsafe32(data + pos);
                    if (ext < 4 || !range_ok(pos, ext, tag_end))
                        return tag_end;
                    pos += ext;
                }
            }

            while (pos < tag_end)
            {
                std::string id;
                std::size_t frame_size = 0;
                u16 frame_flags = 0;
                std::size_t header_size = 0;

                if (version == 2)
                {
                    if (!range_ok(pos, 6, tag_end))
                        break;
                    if (data[pos] == 0 && data[pos + 1] == 0 && data[pos + 2] == 0)
                        break;
                    if (!valid_frame_id(data + pos, 3))
                        break;
                    id.assign(reinterpret_cast<const char *>(data + pos), 3);
                    frame_size = be24(data + pos + 3);
                    header_size = 6;
                }
                else
                {
                    if (!range_ok(pos, 10, tag_end))
                        break;
                    if (data[pos] == 0 && data[pos + 1] == 0 && data[pos + 2] == 0 && data[pos + 3] == 0)
                        break;
                    if (!valid_frame_id(data + pos, 4))
                        break;
                    id.assign(reinterpret_cast<const char *>(data + pos), 4);
                    frame_size = version == 4 ? synchsafe32(data + pos + 4) : be32(data + pos + 4);
                    frame_flags = be16(data + pos + 8);
                    header_size = 10;
                }

                pos += header_size;
                if (frame_size == 0 || !range_ok(pos, frame_size, tag_end))
                    break;
                const u8 *fp = data + pos;
                std::size_t fn = frame_size;
                std::vector<u8> unsynced;

                bool unsupported = false;
                bool frame_unsync = false;
                std::size_t payload_skip = 0;

                if (version == 3)
                {
                    const u8 format_flags = static_cast<u8>(frame_flags & 0xff);
                    if (format_flags & 0x80)
                        unsupported = true; // compression
                    if (format_flags & 0x40)
                        unsupported = true; // encryption
                    if (format_flags & 0x20)
                        payload_skip += 1; // grouping identity
                    frame_unsync = (tag_flags & 0x80) != 0;
                }
                else if (version == 4)
                {
                    const u8 format_flags = static_cast<u8>(frame_flags & 0xff);
                    if (format_flags & 0x08)
                        unsupported = true; // compression
                    if (format_flags & 0x04)
                        unsupported = true; // encryption
                    if (format_flags & 0x40)
                        payload_skip += 1; // grouping identity
                    if (format_flags & 0x01)
                        payload_skip += 4; // data length indicator
                    frame_unsync = ((tag_flags & 0x80) != 0) || ((format_flags & 0x02) != 0);
                }
                else
                {
                    frame_unsync = (tag_flags & 0x80) != 0;
                }

                if (!unsupported && payload_skip <= fn)
                {
                    fp += payload_skip;
                    fn -= payload_skip;
                    if (frame_unsync)
                    {
                        unsynced = deunsync(fp, fn);
                        fp = unsynced.data();
                        fn = unsynced.size();
                    }

                    if (!id.empty() && id[0] == 'T' && id != "TXXX" && id != "TXX")
                    {
                        map_id3_text(m, id, id3_decode_text_frame(fp, fn));
                    }
                    else if (id == "TXXX" || id == "TXX")
                    {
                        if (fn >= 1 && fp[0] <= 3)
                        {
                            const u8 enc = fp[0];
                            std::size_t q = 1;
                            const std::size_t term = encoded_terminator(fp + q, fn - q, enc);
                            std::size_t desc_n = term;
                            if (enc == 0 || enc == 3)
                            {
                                if (desc_n && fp[q + desc_n - 1] == 0)
                                    --desc_n;
                            }
                            else if (desc_n >= 2 && fp[q + desc_n - 2] == 0 && fp[q + desc_n - 1] == 0)
                            {
                                desc_n -= 2;
                            }
                            const std::string desc = id3_decode_text_bytes(enc, fp + q, desc_n);
                            q += term;
                            if (q <= fn)
                            {
                                const std::string value = id3_decode_text_bytes(enc, fp + q, fn - q);
                                if (!value.empty())
                                    add_tag(m, "ID3:TXXX:" + upper_ascii(desc), value);
                                if (!desc.empty() && !value.empty())
                                    add_tag(m, desc, value);
                            }
                        }
                    }
                    else if (id == "COMM" || id == "COM")
                    {
                        const std::string value = parse_id3_comment(fp, fn);
                        if (!value.empty())
                        {
                            add_tag(m, "ID3:" + id, value);
                            apply_common(m, "COMMENT", value);
                        }
                    }
                    else if (id == "APIC" || id == "PIC")
                    {
                        Picture pic;
                        if (parse_apic(fp, fn, id == "PIC", pic))
                            m.pictures.push_back(std::move(pic));
                    }
                }
                pos += frame_size;
            }

            // v2.4 footer, if present, follows the body and is not included in body_size.
            if (version == 4 && (tag_flags & 0x10) && range_ok(tag_end, 10, size) && std::memcmp(data + tag_end, "3DI", 3) == 0)
            {
                return tag_end + 10;
            }
            return tag_end;
        }

        static void parse_id3v1(const u8 *data, std::size_t size, Metadata &m)
        {
            if (size < 128)
                return;
            const u8 *tag = data + size - 128;
            if (std::memcmp(tag, "TAG", 3) != 0)
                return;

            auto field = [&](std::size_t off, std::size_t n)
            {
                return latin1_to_utf8(tag + off, n);
            };
            const std::string title = field(3, 30);
            const std::string artist = field(33, 30);
            const std::string album = field(63, 30);
            const std::string year = field(93, 4);
            const bool v11 = tag[125] == 0 && tag[126] != 0;
            const std::string comment = field(97, v11 ? 28 : 30);

            if (!title.empty())
            {
                add_tag(m, "ID3V1:TITLE", title);
                if (m.title.empty())
                    apply_common(m, "TITLE", title);
            }
            if (!artist.empty())
            {
                add_tag(m, "ID3V1:ARTIST", artist);
                if (m.artist.empty())
                    apply_common(m, "ARTIST", artist);
            }
            if (!album.empty())
            {
                add_tag(m, "ID3V1:ALBUM", album);
                if (m.album.empty())
                    apply_common(m, "ALBUM", album);
            }
            if (!year.empty())
            {
                add_tag(m, "ID3V1:YEAR", year);
                if (m.date.empty())
                    apply_common(m, "YEAR", year);
            }
            if (!comment.empty())
            {
                add_tag(m, "ID3V1:COMMENT", comment);
                if (m.comment.empty())
                    apply_common(m, "COMMENT", comment);
            }
            if (v11 && !m.track_number)
                m.track_number = tag[126];
            const std::string genre = id3_genre_name(tag[127]);
            if (!genre.empty())
            {
                add_tag(m, "ID3V1:GENRE", genre);
                if (m.genre.empty())
                    apply_common(m, "GENRE", genre);
            }

            // Enhanced ID3v1 (TAG+) immediately before the normal ID3v1 tag.
            if (size >= 355)
            {
                const u8 *ext = data + size - 355;
                if (std::memcmp(ext, "TAG+", 4) == 0)
                {
                    const std::string xtitle = latin1_to_utf8(ext + 4, 60);
                    const std::string xartist = latin1_to_utf8(ext + 64, 60);
                    const std::string xalbum = latin1_to_utf8(ext + 124, 60);
                    const std::string xgenre = latin1_to_utf8(ext + 185, 30);
                    if (!xtitle.empty())
                    {
                        add_tag(m, "ID3V1+:TITLE", xtitle);
                        if (m.title.empty())
                            m.title = xtitle;
                    }
                    if (!xartist.empty())
                    {
                        add_tag(m, "ID3V1+:ARTIST", xartist);
                        if (m.artist.empty())
                            m.artist = xartist;
                    }
                    if (!xalbum.empty())
                    {
                        add_tag(m, "ID3V1+:ALBUM", xalbum);
                        if (m.album.empty())
                            m.album = xalbum;
                    }
                    if (!xgenre.empty())
                    {
                        add_tag(m, "ID3V1+:GENRE", xgenre);
                        if (m.genre.empty())
                            m.genre = xgenre;
                    }
                }
            }
        }

        static std::size_t end_without_id3v1(const u8 *data, std::size_t size)
        {
            if (size >= 128 && std::memcmp(data + size - 128, "TAG", 3) == 0)
                return size - 128;
            return size;
        }

        static bool parse_ape_tags(const u8 *data, std::size_t size, Metadata &m, bool overwrite_common = false)
        {
            std::size_t end = end_without_id3v1(data, size);
            if (end < 32 || std::memcmp(data + end - 32, "APETAGEX", 8) != 0)
                return false;
            const u8 *footer = data + end - 32;
            const u32 version = le32(footer + 8);
            const u32 tag_size = le32(footer + 12);
            const u32 item_count = le32(footer + 16);
            if ((version != 1000 && version != 2000) || tag_size < 32 || tag_size > end)
                return false;

            const std::size_t item_start = end - tag_size;
            const std::size_t item_end = end - 32;
            std::size_t pos = item_start;
            for (u32 item = 0; item < item_count; ++item)
            {
                if (!range_ok(pos, 8, item_end))
                    break;
                const u32 value_size = le32(data + pos);
                const u32 flags = le32(data + pos + 4);
                pos += 8;
                const std::size_t key_start = pos;
                while (pos < item_end && data[pos])
                    ++pos;
                if (pos >= item_end)
                    break;
                std::string key(reinterpret_cast<const char *>(data + key_start), pos - key_start);
                ++pos;
                if (!range_ok(pos, value_size, item_end))
                    break;
                const u8 *value = data + pos;
                pos += value_size;
                if (key.empty())
                    continue;

                const u32 type = (flags >> 1) & 3u;
                const std::string ukey = upper_ascii(key);
                if (type == 0)
                {
                    std::size_t vpos = 0;
                    while (vpos <= value_size)
                    {
                        std::size_t vend = vpos;
                        while (vend < value_size && value[vend] != 0)
                            ++vend;
                        std::string s(reinterpret_cast<const char *>(value + vpos), vend - vpos);
                        s = trim_ascii(s);
                        if (!s.empty())
                        {
                            add_tag(m, "APE:" + ukey, s);
                            apply_common(m, ukey, s, overwrite_common);
                        }
                        if (vend >= value_size)
                            break;
                        vpos = vend + 1;
                    }
                }
                else if (type == 1 && ukey.rfind("COVER ART (", 0) == 0)
                {
                    std::size_t z = 0;
                    while (z < value_size && value[z])
                        ++z;
                    Picture pic;
                    pic.description.assign(reinterpret_cast<const char *>(value), z);
                    if (ukey.find("FRONT") != std::string::npos)
                        pic.type = 3;
                    else if (ukey.find("BACK") != std::string::npos)
                        pic.type = 4;
                    else
                        pic.type = 0;
                    if (z < value_size)
                        ++z;
                    if (z < value_size)
                    {
                        pic.data.assign(value + z, value + value_size);
                        pic.mime = mime_from_image(pic.data.data(), pic.data.size());
                        fill_image_dimensions(pic);
                        m.pictures.push_back(std::move(pic));
                    }
                }
            }
            return true;
        }

        static void parse_monkeys_audio_info(const u8 *data, std::size_t size, std::size_t start, Metadata &m)
        {
            if (!range_ok(start, 16, size) || std::memcmp(data + start, "MAC ", 4) != 0)
                return;
            const u16 version = le16(data + start + 4);
            u32 blocks_per_frame = 0;
            u32 final_frame_blocks = 0;
            u32 total_frames = 0;

            if (version >= 3980)
            {
                if (!range_ok(start, 52, size))
                    return;
                const u32 descriptor_bytes = le32(data + start + 8);
                const u32 header_bytes = le32(data + start + 12);
                if (descriptor_bytes < 52 || header_bytes < 24 || !range_ok(start, static_cast<std::size_t>(descriptor_bytes) + 24, size))
                    return;
                const u8 *h = data + start + descriptor_bytes;
                blocks_per_frame = le32(h + 4);
                final_frame_blocks = le32(h + 8);
                total_frames = le32(h + 12);
                m.bits_per_sample = le16(h + 16);
                m.channels = le16(h + 18);
                m.sample_rate = le32(h + 20);
            }
            else
            {
                if (!range_ok(start, 32, size))
                    return;
                const u16 compression = le16(data + start + 6);
                const u16 format_flags = le16(data + start + 8);
                m.channels = le16(data + start + 10);
                m.sample_rate = le32(data + start + 12);
                total_frames = le32(data + start + 24);
                final_frame_blocks = le32(data + start + 28);
                if (version >= 3950)
                    blocks_per_frame = 73728u * 4u;
                else if (version >= 3900 || (version >= 3800 && compression >= 4000))
                    blocks_per_frame = 73728u;
                else
                    blocks_per_frame = 9216u;
                if (format_flags & 0x0001)
                    m.bits_per_sample = 8;
                else if (format_flags & 0x0008)
                    m.bits_per_sample = 24;
                else
                    m.bits_per_sample = 16;
            }

            if (m.sample_rate && total_frames)
            {
                const u64 total_blocks = static_cast<u64>(total_frames - 1) * blocks_per_frame + final_frame_blocks;
                m.duration_seconds = static_cast<double>(total_blocks) / static_cast<double>(m.sample_rate);
            }
        }

        static bool parse_vorbis_comments(const u8 *p, std::size_t n, Metadata &m, bool overwrite_common)
        {
            std::size_t pos = 0;
            if (!range_ok(pos, 4, n))
                return false;
            const u32 vendor_len = le32(p + pos);
            pos += 4;
            if (!range_ok(pos, vendor_len, n))
                return false;
            if (vendor_len)
                add_tag(m, "VENDOR", std::string(reinterpret_cast<const char *>(p + pos), vendor_len));
            pos += vendor_len;
            if (!range_ok(pos, 4, n))
                return false;
            const u32 count = le32(p + pos);
            pos += 4;

            std::vector<std::string> legacy_coverart;
            std::string legacy_mime;
            for (u32 i = 0; i < count; ++i)
            {
                if (!range_ok(pos, 4, n))
                    return false;
                const u32 len = le32(p + pos);
                pos += 4;
                if (!range_ok(pos, len, n))
                    return false;
                std::string entry(reinterpret_cast<const char *>(p + pos), len);
                pos += len;
                const std::size_t eq = entry.find('=');
                if (eq == std::string::npos || eq == 0)
                    continue;
                const std::string key = upper_ascii(entry.substr(0, eq));
                const std::string value = entry.substr(eq + 1);
                if (key == "METADATA_BLOCK_PICTURE")
                {
                    const std::vector<u8> decoded = base64_decode(value);
                    Picture pic;
                    if (!decoded.empty() && parse_flac_picture_block(decoded.data(), decoded.size(), pic))
                        m.pictures.push_back(std::move(pic));
                }
                else if (key == "COVERART")
                {
                    legacy_coverart.push_back(value);
                }
                else
                {
                    add_tag(m, key, value);
                    apply_common(m, key, value, overwrite_common);
                    if (key == "COVERARTMIME" && legacy_mime.empty())
                        legacy_mime = value;
                }
            }

            for (const std::string &b64 : legacy_coverart)
            {
                std::vector<u8> decoded = base64_decode(b64);
                if (decoded.empty())
                    continue;
                Picture pic;
                pic.type = 3;
                pic.mime = legacy_mime.empty() ? mime_from_image(decoded.data(), decoded.size()) : legacy_mime;
                pic.data = std::move(decoded);
                fill_image_dimensions(pic);
                m.pictures.push_back(std::move(pic));
            }
            return true;
        }

        static bool parse_flac(const u8 *data, std::size_t size, std::size_t start, Metadata &m)
        {
            if (!range_ok(start, 4, size) || std::memcmp(data + start, "fLaC", 4) != 0)
                return false;
            m.format = Format::flac;
            std::size_t pos = start + 4;
            bool last = false;
            while (!last && range_ok(pos, 4, size))
            {
                const u8 h = data[pos];
                last = (h & 0x80) != 0;
                const u8 type = h & 0x7f;
                const u32 len = be24(data + pos + 1);
                pos += 4;
                if (!range_ok(pos, len, size))
                    return false;
                const u8 *block = data + pos;
                if (type == 0 && len >= 34)
                {
                    const u32 sr = (static_cast<u32>(block[10]) << 12) |
                                   (static_cast<u32>(block[11]) << 4) |
                                   (static_cast<u32>(block[12]) >> 4);
                    const u32 ch = ((block[12] >> 1) & 7u) + 1u;
                    const u32 bps = (((static_cast<u32>(block[12]) & 1u) << 4) | (block[13] >> 4)) + 1u;
                    const u64 total_samples = (static_cast<u64>(block[13] & 0x0f) << 32) | be32(block + 14);
                    m.sample_rate = sr;
                    m.channels = ch;
                    m.bits_per_sample = bps;
                    if (sr && total_samples)
                        m.duration_seconds = static_cast<double>(total_samples) / sr;
                }
                else if (type == 4)
                {
                    parse_vorbis_comments(block, len, m, true);
                }
                else if (type == 6)
                {
                    Picture pic;
                    if (parse_flac_picture_block(block, len, pic))
                        m.pictures.push_back(std::move(pic));
                }
                pos += len;
            }
            if (m.duration_seconds > 0.0 && size > start)
            {
                m.bitrate_kbps = static_cast<u32>(((static_cast<double>(size - start) * 8.0) / m.duration_seconds) / 1000.0 + 0.5);
            }
            return true;
        }

        struct OggPacketState
        {
            u32 serial = 0;
            bool have_serial = false;
            std::vector<u8> packet;
            std::vector<std::vector<u8>> packets;
            u64 max_granule = 0;
        };

        static bool parse_ogg(const u8 *data, std::size_t size, std::size_t start, Metadata &m)
        {
            if (!range_ok(start, 27, size) || std::memcmp(data + start, "OggS", 4) != 0)
                return false;
            OggPacketState st;
            std::size_t pos = start;
            while (range_ok(pos, 27, size) && std::memcmp(data + pos, "OggS", 4) == 0)
            {
                if (data[pos + 4] != 0)
                    return false;
                const u8 header_type = data[pos + 5];
                const u64 granule = le64(data + pos + 6);
                const u32 serial = le32(data + pos + 14);
                const u8 segs = data[pos + 26];
                if (!range_ok(pos + 27, segs, size))
                    return false;
                std::size_t body_len = 0;
                for (u8 i = 0; i < segs; ++i)
                    body_len += data[pos + 27 + i];
                const std::size_t body = pos + 27 + segs;
                if (!range_ok(body, body_len, size))
                    return false;

                if (!st.have_serial)
                {
                    st.serial = serial;
                    st.have_serial = true;
                }
                if (serial == st.serial)
                {
                    if (granule != std::numeric_limits<u64>::max() && granule > st.max_granule)
                        st.max_granule = granule;
                    if (!(header_type & 0x01) && !st.packet.empty())
                        st.packet.clear();
                    std::size_t q = body;
                    for (u8 i = 0; i < segs; ++i)
                    {
                        const u8 len = data[pos + 27 + i];
                        if (st.packets.size() < 3)
                            st.packet.insert(st.packet.end(), data + q, data + q + len);
                        q += len;
                        if (len < 255)
                        {
                            if (st.packets.size() < 3)
                            {
                                st.packets.push_back(std::move(st.packet));
                                st.packet.clear();
                            }
                        }
                    }
                }
                pos = body + body_len;
            }

            if (st.packets.empty())
                return false;
            const auto &first = st.packets[0];
            if (first.size() >= 30 && first[0] == 1 && std::memcmp(first.data() + 1, "vorbis", 6) == 0)
            {
                m.format = Format::ogg_vorbis;
                m.channels = first[11];
                m.sample_rate = le32(first.data() + 12);
                if (st.packets.size() >= 2)
                {
                    const auto &c = st.packets[1];
                    if (c.size() >= 7 && c[0] == 3 && std::memcmp(c.data() + 1, "vorbis", 6) == 0)
                    {
                        parse_vorbis_comments(c.data() + 7, c.size() - 7, m, true);
                    }
                }
                if (m.sample_rate && st.max_granule)
                    m.duration_seconds = static_cast<double>(st.max_granule) / m.sample_rate;
            }
            else if (first.size() >= 19 && std::memcmp(first.data(), "OpusHead", 8) == 0)
            {
                m.format = Format::opus;
                m.channels = first[9];
                const u16 pre_skip = le16(first.data() + 10);
                m.sample_rate = 48000;
                if (st.packets.size() >= 2)
                {
                    const auto &c = st.packets[1];
                    if (c.size() >= 8 && std::memcmp(c.data(), "OpusTags", 8) == 0)
                    {
                        parse_vorbis_comments(c.data() + 8, c.size() - 8, m, true);
                    }
                }
                if (st.max_granule > pre_skip)
                    m.duration_seconds = static_cast<double>(st.max_granule - pre_skip) / 48000.0;
            }
            else
            {
                return false;
            }

            if (m.duration_seconds > 0.0 && size > start)
            {
                m.bitrate_kbps = static_cast<u32>(((static_cast<double>(size - start) * 8.0) / m.duration_seconds) / 1000.0 + 0.5);
            }
            return true;
        }

        struct MpegFrameInfo
        {
            u32 bitrate_kbps = 0;
            u32 sample_rate = 0;
            u32 samples = 0;
            u32 frame_bytes = 0;
            u32 channels = 0;
        };

        static bool parse_mpeg_header(const u8 *p, std::size_t n, MpegFrameInfo &f)
        {
            if (n < 4 || p[0] != 0xff || (p[1] & 0xe0) != 0xe0)
                return false;
            const u8 version_bits = (p[1] >> 3) & 3;
            const u8 layer_bits = (p[1] >> 1) & 3;
            const u8 bitrate_idx = (p[2] >> 4) & 0x0f;
            const u8 rate_idx = (p[2] >> 2) & 3;
            const u8 padding = (p[2] >> 1) & 1;
            if (version_bits == 1 || layer_bits == 0 || bitrate_idx == 0 || bitrate_idx == 15 || rate_idx == 3)
                return false;

            const bool mpeg1 = version_bits == 3;
            const int layer = 4 - layer_bits; // bits 3,2,1 => layers I,II,III
            static const u16 br_mpeg1[3][14] = {
                {32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
                {32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
                {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}};
            static const u16 br_mpeg2[3][14] = {
                {32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
                {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
                {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}};
            static const u32 base_rates[3] = {44100, 48000, 32000};
            u32 rate = base_rates[rate_idx];
            if (version_bits == 2)
                rate /= 2;
            else if (version_bits == 0)
                rate /= 4;

            const u32 br = mpeg1 ? br_mpeg1[layer - 1][bitrate_idx - 1] : br_mpeg2[layer - 1][bitrate_idx - 1];
            if (!br || !rate)
                return false;

            u32 frame_bytes = 0;
            u32 samples = 0;
            if (layer == 1)
            {
                frame_bytes = ((12u * br * 1000u) / rate + padding) * 4u;
                samples = 384;
            }
            else if (layer == 3 && !mpeg1)
            {
                frame_bytes = (72u * br * 1000u) / rate + padding;
                samples = 576;
            }
            else
            {
                frame_bytes = (144u * br * 1000u) / rate + padding;
                samples = 1152;
            }
            if (frame_bytes < 4)
                return false;

            f.bitrate_kbps = br;
            f.sample_rate = rate;
            f.samples = samples;
            f.frame_bytes = frame_bytes;
            f.channels = ((p[3] >> 6) & 3) == 3 ? 1u : 2u;
            return true;
        }

        static std::size_t find_first_mpeg_frame(const u8 *data, std::size_t begin, std::size_t end, MpegFrameInfo &first)
        {
            for (std::size_t pos = begin; pos + 4 <= end; ++pos)
            {
                MpegFrameInfo f;
                if (!parse_mpeg_header(data + pos, end - pos, f))
                    continue;
                if (pos + f.frame_bytes > end)
                    continue;
                if (pos + f.frame_bytes + 4 <= end)
                {
                    MpegFrameInfo next;
                    if (!parse_mpeg_header(data + pos + f.frame_bytes, end - pos - f.frame_bytes, next))
                        continue;
                    if (next.sample_rate != f.sample_rate)
                        continue;
                }
                first = f;
                return pos;
            }
            return end;
        }

        static bool mpeg_gapless_samples_from_xing(const u8 *frame,
                                                   std::size_t frame_size,
                                                   const MpegFrameInfo &info,
                                                   u64 &playable_samples)
        {
            if (frame_size < 4 || info.samples == 0)
                return false;
            const u8 version_bits = (frame[1] >> 3) & 3;
            const u8 layer_bits = (frame[1] >> 1) & 3;
            if (version_bits == 1 || layer_bits != 1)
                return false; // Layer III only
            const bool mpeg1 = version_bits == 3;
            const bool mono = ((frame[3] >> 6) & 3) == 3;
            const std::size_t side_info = mpeg1 ? (mono ? 17u : 32u) : (mono ? 9u : 17u);
            std::size_t pos = 4 + side_info;
            if (!range_ok(pos, 8, frame_size))
                return false;
            if (std::memcmp(frame + pos, "Xing", 4) != 0 && std::memcmp(frame + pos, "Info", 4) != 0)
                return false;
            pos += 4;
            const u32 flags = be32(frame + pos);
            pos += 4;

            u32 frames = 0;
            if (flags & 0x01)
            {
                if (!range_ok(pos, 4, frame_size))
                    return false;
                frames = be32(frame + pos);
                pos += 4;
            }
            if (flags & 0x02)
            {
                if (!range_ok(pos, 4, frame_size))
                    return false;
                pos += 4;
            }
            if (flags & 0x04)
            {
                if (!range_ok(pos, 100, frame_size))
                    return false;
                pos += 100;
            }
            if (flags & 0x08)
            {
                if (!range_ok(pos, 4, frame_size))
                    return false;
                pos += 4;
            }
            if (!frames)
                return false;

            u64 total = static_cast<u64>(frames) * info.samples;
            // The LAME/Info extension places the 12-bit encoder delay and 12-bit
            // end padding at bytes 21..23 after the extension begins. FFmpeg also
            // writes this layout even when its encoder string is "Lavc...".
            if (range_ok(pos, 24, frame_size))
            {
                const u32 delay = (static_cast<u32>(frame[pos + 21]) << 4) | (frame[pos + 22] >> 4);
                const u32 padding = (static_cast<u32>(frame[pos + 22] & 0x0f) << 8) | frame[pos + 23];
                if (delay <= 3000 && padding <= 3000 && total > static_cast<u64>(delay) + padding)
                {
                    total -= static_cast<u64>(delay) + padding;
                }
            }
            playable_samples = total;
            return true;
        }

        static u32 mp4_fourcc(u8 a, u8 b, u8 c, u8 d)
        {
            return (static_cast<u32>(a) << 24) | (static_cast<u32>(b) << 16) |
                   (static_cast<u32>(c) << 8) | static_cast<u32>(d);
        }

        struct Mp4Atom
        {
            std::size_t start = 0;
            std::size_t payload = 0;
            std::size_t end = 0;
            u32 type = 0;
        };

        static bool mp4_atom_at(const u8 *data, std::size_t pos, std::size_t limit, Mp4Atom &atom)
        {
            if (!range_ok(pos, 8, limit))
                return false;
            const u32 size32 = be32(data + pos);
            const u32 type = be32(data + pos + 4);
            std::size_t header = 8;
            u64 total = size32;
            if (size32 == 1)
            {
                if (!range_ok(pos, 16, limit))
                    return false;
                total = be64(data + pos + 8);
                header = 16;
            }
            else if (size32 == 0)
            {
                total = static_cast<u64>(limit - pos);
            }
            if (total < header || total > static_cast<u64>(limit - pos))
                return false;
            atom.start = pos;
            atom.payload = pos + header;
            atom.end = pos + static_cast<std::size_t>(total);
            atom.type = type;
            return true;
        }

        static std::string mp4_fourcc_name(u32 type)
        {
            const u8 a = static_cast<u8>(type >> 24);
            const u8 b = static_cast<u8>(type >> 16);
            const u8 c = static_cast<u8>(type >> 8);
            const u8 d = static_cast<u8>(type);
            if (a == 0xa9 && b >= 0x20 && c >= 0x20 && d >= 0x20)
            {
                std::string out("\xc2\xa9");
                out.push_back(static_cast<char>(b));
                out.push_back(static_cast<char>(c));
                out.push_back(static_cast<char>(d));
                return out;
            }
            if (a >= 0x20 && a <= 0x7e && b >= 0x20 && b <= 0x7e &&
                c >= 0x20 && c <= 0x7e && d >= 0x20 && d <= 0x7e)
            {
                std::string out;
                out.push_back(static_cast<char>(a));
                out.push_back(static_cast<char>(b));
                out.push_back(static_cast<char>(c));
                out.push_back(static_cast<char>(d));
                return out;
            }
            static const char hex[] = "0123456789ABCDEF";
            std::string out("0x");
            for (int shift = 28; shift >= 0; shift -= 4)
                out.push_back(hex[(type >> shift) & 15]);
            return out;
        }

        struct Mp4DataValue
        {
            u32 type = 0;
            const u8 *data = nullptr;
            std::size_t size = 0;
        };

        static bool mp4_data_value(const u8 *data, const Mp4Atom &atom, Mp4DataValue &value)
        {
            if (atom.type != mp4_fourcc('d', 'a', 't', 'a') || atom.end - atom.payload < 8)
                return false;
            // iTunes/QuickTime metadata uses the first 32 bits as a type indicator
            // (commonly encoded in the low 24 bits), followed by a 32-bit locale.
            value.type = be32(data + atom.payload) & 0x00ffffffu;
            value.data = data + atom.payload + 8;
            value.size = atom.end - (atom.payload + 8);
            return true;
        }

        static std::string mp4_text_value(const Mp4DataValue &value)
        {
            if (!value.data || !value.size)
                return {};
            if (value.type == 2)
                return utf16_to_utf8(value.data, value.size, true, true);
            std::size_t n = value.size;
            while (n && value.data[n - 1] == 0)
                --n;
            return trim_ascii(std::string(reinterpret_cast<const char *>(value.data), n));
        }

        static std::string mp4_integer_value(const Mp4DataValue &value)
        {
            if (!value.data || value.size == 0 || value.size > 8)
                return {};
            u64 v = 0;
            for (std::size_t i = 0; i < value.size; ++i)
                v = (v << 8) | value.data[i];
            return std::to_string(v);
        }

        static void mp4_add_text(Metadata &m,
                                 const std::string &raw_key,
                                 const std::string &common_key,
                                 const std::string &value)
        {
            if (value.empty())
                return;
            add_tag(m, raw_key, value);
            if (!common_key.empty())
                apply_common(m, common_key, value, true);
        }

        static std::string mp4_common_key_for_fourcc(u32 type)
        {
            if (type == mp4_fourcc(static_cast<u8>(0xa9), 'n', 'a', 'm'))
                return "TITLE";
            if (type == mp4_fourcc(static_cast<u8>(0xa9), 'A', 'R', 'T'))
                return "ARTIST";
            if (type == mp4_fourcc('a', 'A', 'R', 'T'))
                return "ALBUMARTIST";
            if (type == mp4_fourcc(static_cast<u8>(0xa9), 'a', 'l', 'b'))
                return "ALBUM";
            if (type == mp4_fourcc(static_cast<u8>(0xa9), 'w', 'r', 't'))
                return "COMPOSER";
            if (type == mp4_fourcc(static_cast<u8>(0xa9), 'g', 'e', 'n'))
                return "GENRE";
            if (type == mp4_fourcc(static_cast<u8>(0xa9), 'd', 'a', 'y'))
                return "DATE";
            if (type == mp4_fourcc(static_cast<u8>(0xa9), 'c', 'm', 't'))
                return "COMMENT";
            return {};
        }

        static std::string mp4_common_key_for_mdta(const std::string &key)
        {
            const std::string u = upper_ascii(key);
            if (u == "COM.APPLE.QUICKTIME.TITLE" || u == "TITLE")
                return "TITLE";
            if (u == "COM.APPLE.QUICKTIME.ARTIST" || u == "ARTIST")
                return "ARTIST";
            if (u == "COM.APPLE.QUICKTIME.ALBUM" || u == "ALBUM")
                return "ALBUM";
            if (u == "COM.APPLE.QUICKTIME.ALBUMARTIST" || u == "ALBUMARTIST" || u == "ALBUM ARTIST")
                return "ALBUMARTIST";
            if (u == "COM.APPLE.QUICKTIME.COMPOSER" || u == "COMPOSER")
                return "COMPOSER";
            if (u == "COM.APPLE.QUICKTIME.GENRE" || u == "GENRE")
                return "GENRE";
            if (u == "COM.APPLE.QUICKTIME.CREATIONDATE" || u == "DATE" || u == "YEAR")
                return "DATE";
            if (u == "COM.APPLE.QUICKTIME.DESCRIPTION" || u == "COM.APPLE.QUICKTIME.COMMENT" || u == "COMMENT")
                return "COMMENT";
            if (u == "TRACKNUMBER" || u == "TRACK")
                return "TRACKNUMBER";
            if (u == "DISCNUMBER" || u == "DISC")
                return "DISCNUMBER";
            return {};
        }

        static void mp4_parse_item(const u8 *data,
                                   const Mp4Atom &item,
                                   Metadata &m,
                                   const std::string &mdta_key)
        {
            std::string mean;
            std::string name;
            std::vector<Mp4DataValue> values;
            std::size_t pos = item.payload;
            while (pos < item.end)
            {
                Mp4Atom child;
                if (!mp4_atom_at(data, pos, item.end, child))
                    break;
                if (child.type == mp4_fourcc('d', 'a', 't', 'a'))
                {
                    Mp4DataValue value;
                    if (mp4_data_value(data, child, value))
                        values.push_back(value);
                }
                else if (child.type == mp4_fourcc('m', 'e', 'a', 'n') || child.type == mp4_fourcc('n', 'a', 'm', 'e'))
                {
                    if (child.end - child.payload >= 4)
                    {
                        const u8 *p = data + child.payload + 4;
                        std::size_t n = child.end - child.payload - 4;
                        while (n && p[n - 1] == 0)
                            --n;
                        const std::string text(reinterpret_cast<const char *>(p), n);
                        if (child.type == mp4_fourcc('m', 'e', 'a', 'n'))
                            mean = text;
                        else
                            name = text;
                    }
                }
                pos = child.end;
            }
            if (values.empty())
                return;

            const bool freeform = item.type == mp4_fourcc('-', '-', '-', '-');
            const std::string fourcc_name = mp4_fourcc_name(item.type);
            std::string raw_key;
            std::string common_key;
            if (!mdta_key.empty())
            {
                raw_key = "MP4:MDTA:" + mdta_key;
                common_key = mp4_common_key_for_mdta(mdta_key);
            }
            else if (freeform)
            {
                raw_key = "MP4:----";
                if (!mean.empty())
                    raw_key += ":" + mean;
                if (!name.empty())
                    raw_key += ":" + name;
                common_key = mp4_common_key_for_mdta(name);
            }
            else
            {
                raw_key = "MP4:" + fourcc_name;
                common_key = mp4_common_key_for_fourcc(item.type);
            }

            for (const Mp4DataValue &value : values)
            {
                if (item.type == mp4_fourcc('c', 'o', 'v', 'r') || value.type == 13 || value.type == 14 || value.type == 27)
                {
                    if (!value.data || !value.size)
                        continue;
                    Picture pic;
                    pic.type = 3;
                    if (value.type == 13)
                        pic.mime = "image/jpeg";
                    else if (value.type == 14)
                        pic.mime = "image/png";
                    else if (value.type == 27)
                        pic.mime = "image/bmp";
                    else
                        pic.mime = mime_from_image(value.data, value.size);
                    pic.data.assign(value.data, value.data + value.size);
                    fill_image_dimensions(pic);
                    m.pictures.push_back(std::move(pic));
                    continue;
                }

                if (item.type == mp4_fourcc('t', 'r', 'k', 'n') && value.size >= 6)
                {
                    const u32 current = be16(value.data + 2);
                    const u32 total = be16(value.data + 4);
                    std::string text = std::to_string(current);
                    if (total)
                        text += "/" + std::to_string(total);
                    add_tag(m, raw_key, text);
                    if (current)
                        m.track_number = current;
                    if (total)
                        m.track_total = total;
                    continue;
                }
                if (item.type == mp4_fourcc('d', 'i', 's', 'k') && value.size >= 6)
                {
                    const u32 current = be16(value.data + 2);
                    const u32 total = be16(value.data + 4);
                    std::string text = std::to_string(current);
                    if (total)
                        text += "/" + std::to_string(total);
                    add_tag(m, raw_key, text);
                    if (current)
                        m.disc_number = current;
                    if (total)
                        m.disc_total = total;
                    continue;
                }
                if (item.type == mp4_fourcc('g', 'n', 'r', 'e') && value.size >= 2)
                {
                    const u16 genre_index = be16(value.data);
                    const std::string genre = genre_index ? id3_genre_name(static_cast<u8>(genre_index - 1)) : std::string();
                    if (!genre.empty())
                        mp4_add_text(m, raw_key, "GENRE", genre);
                    continue;
                }

                std::string text;
                if (value.type == 1 || value.type == 2 || value.type == 0)
                    text = mp4_text_value(value);
                else if ((value.type >= 21 && value.type <= 22) || (value.type >= 65 && value.type <= 74))
                    text = mp4_integer_value(value);
                else if (!common_key.empty() || freeform || !mdta_key.empty())
                    text = mp4_text_value(value);
                if (!text.empty())
                    mp4_add_text(m, raw_key, common_key, text);
            }
        }

        static bool mp4_parse_keys(const u8 *data, const Mp4Atom &keys, std::vector<std::string> &out)
        {
            if (keys.end - keys.payload < 8)
                return false;
            const u32 count = be32(data + keys.payload + 4);
            std::size_t pos = keys.payload + 8;
            out.clear();
            out.reserve(count);
            for (u32 i = 0; i < count; ++i)
            {
                if (!range_ok(pos, 8, keys.end))
                    return false;
                const u32 entry_size = be32(data + pos);
                if (entry_size < 8 || !range_ok(pos, entry_size, keys.end))
                    return false;
                // The namespace is normally 'mdta'. Keep the key text independent of it.
                out.emplace_back(reinterpret_cast<const char *>(data + pos + 8), entry_size - 8);
                pos += entry_size;
            }
            return true;
        }

        static void mp4_parse_ilst(const u8 *data,
                                   const Mp4Atom &ilst,
                                   Metadata &m,
                                   const std::vector<std::string> &keys)
        {
            std::size_t pos = ilst.payload;
            while (pos < ilst.end)
            {
                Mp4Atom item;
                if (!mp4_atom_at(data, pos, ilst.end, item))
                    break;
                std::string mdta_key;
                if (!keys.empty() && item.type >= 1 && item.type <= keys.size())
                    mdta_key = keys[item.type - 1];
                mp4_parse_item(data, item, m, mdta_key);
                pos = item.end;
            }
        }

        static void mp4_parse_meta(const u8 *data, const Mp4Atom &meta, Metadata &m)
        {
            if (meta.end - meta.payload < 4)
                return;
            const std::size_t begin = meta.payload + 4; // full-box version/flags
            std::vector<std::string> keys;
            std::size_t pos = begin;
            while (pos < meta.end)
            {
                Mp4Atom child;
                if (!mp4_atom_at(data, pos, meta.end, child))
                    break;
                if (child.type == mp4_fourcc('k', 'e', 'y', 's'))
                    mp4_parse_keys(data, child, keys);
                pos = child.end;
            }
            pos = begin;
            while (pos < meta.end)
            {
                Mp4Atom child;
                if (!mp4_atom_at(data, pos, meta.end, child))
                    break;
                if (child.type == mp4_fourcc('i', 'l', 's', 't'))
                    mp4_parse_ilst(data, child, m, keys);
                pos = child.end;
            }
        }

        static void mp4_parse_mvhd(const u8 *data, const Mp4Atom &atom, Metadata &m)
        {
            const std::size_t n = atom.end - atom.payload;
            if (n < 20)
                return;
            const u8 version = data[atom.payload];
            u32 timescale = 0;
            u64 duration = 0;
            if (version == 0 && n >= 20)
            {
                timescale = be32(data + atom.payload + 12);
                duration = be32(data + atom.payload + 16);
            }
            else if (version == 1 && n >= 32)
            {
                timescale = be32(data + atom.payload + 20);
                duration = be64(data + atom.payload + 24);
            }
            if (timescale && duration && duration != std::numeric_limits<u64>::max())
                m.duration_seconds = static_cast<double>(duration) / timescale;
        }

        static bool mp4_mdia_is_audio(const u8 *data, const Mp4Atom &mdia)
        {
            std::size_t pos = mdia.payload;
            while (pos < mdia.end)
            {
                Mp4Atom child;
                if (!mp4_atom_at(data, pos, mdia.end, child))
                    return false;
                if (child.type == mp4_fourcc('h', 'd', 'l', 'r') && child.end - child.payload >= 12)
                    return be32(data + child.payload + 8) == mp4_fourcc('s', 'o', 'u', 'n');
                pos = child.end;
            }
            return false;
        }

        static void mp4_parse_mdhd(const u8 *data, const Mp4Atom &atom, Metadata &m)
        {
            const std::size_t n = atom.end - atom.payload;
            if (n < 20)
                return;
            const u8 version = data[atom.payload];
            u32 timescale = 0;
            u64 duration = 0;
            if (version == 0 && n >= 20)
            {
                timescale = be32(data + atom.payload + 12);
                duration = be32(data + atom.payload + 16);
            }
            else if (version == 1 && n >= 32)
            {
                timescale = be32(data + atom.payload + 20);
                duration = be64(data + atom.payload + 24);
            }
            if (timescale && !m.sample_rate)
                m.sample_rate = timescale;
            if (timescale && duration && m.duration_seconds <= 0.0)
                m.duration_seconds = static_cast<double>(duration) / timescale;
        }

        static void mp4_parse_stsd(const u8 *data, const Mp4Atom &atom, Metadata &m)
        {
            if (atom.end - atom.payload < 8)
                return;
            const u32 count = be32(data + atom.payload + 4);
            std::size_t pos = atom.payload + 8;
            for (u32 i = 0; i < count && pos < atom.end; ++i)
            {
                Mp4Atom entry;
                if (!mp4_atom_at(data, pos, atom.end, entry))
                    break;
                const std::size_t n = entry.end - entry.payload;
                if (n >= 28)
                {
                    const u16 version = be16(data + entry.payload + 8);
                    if (version <= 1)
                    {
                        const u16 channels = be16(data + entry.payload + 16);
                        const u16 bits = be16(data + entry.payload + 18);
                        const u32 fixed_rate = be32(data + entry.payload + 24);
                        if (channels)
                            m.channels = channels;
                        if (bits)
                            m.bits_per_sample = bits;
                        if (fixed_rate >> 16)
                            m.sample_rate = fixed_rate >> 16;
                    }
                }
                pos = entry.end;
            }
        }

        static void mp4_find_stsd(const u8 *data, const Mp4Atom &parent, Metadata &m, int depth)
        {
            if (depth > 4)
                return;
            std::size_t pos = parent.payload;
            while (pos < parent.end)
            {
                Mp4Atom child;
                if (!mp4_atom_at(data, pos, parent.end, child))
                    break;
                if (child.type == mp4_fourcc('s', 't', 's', 'd'))
                {
                    mp4_parse_stsd(data, child, m);
                }
                else if (child.type == mp4_fourcc('m', 'i', 'n', 'f') || child.type == mp4_fourcc('s', 't', 'b', 'l'))
                {
                    mp4_find_stsd(data, child, m, depth + 1);
                }
                pos = child.end;
            }
        }

        static void mp4_parse_trak(const u8 *data, const Mp4Atom &trak, Metadata &m)
        {
            std::size_t pos = trak.payload;
            while (pos < trak.end)
            {
                Mp4Atom child;
                if (!mp4_atom_at(data, pos, trak.end, child))
                    break;
                if (child.type == mp4_fourcc('m', 'd', 'i', 'a') && mp4_mdia_is_audio(data, child))
                {
                    std::size_t q = child.payload;
                    while (q < child.end)
                    {
                        Mp4Atom mdia_child;
                        if (!mp4_atom_at(data, q, child.end, mdia_child))
                            break;
                        if (mdia_child.type == mp4_fourcc('m', 'd', 'h', 'd'))
                            mp4_parse_mdhd(data, mdia_child, m);
                        else if (mdia_child.type == mp4_fourcc('m', 'i', 'n', 'f'))
                            mp4_find_stsd(data, mdia_child, m, 0);
                        q = mdia_child.end;
                    }
                    return;
                }
                pos = child.end;
            }
        }

        static void mp4_parse_udta(const u8 *data, const Mp4Atom &udta, Metadata &m)
        {
            std::size_t pos = udta.payload;
            while (pos < udta.end)
            {
                Mp4Atom child;
                if (!mp4_atom_at(data, pos, udta.end, child))
                    break;
                if (child.type == mp4_fourcc('m', 'e', 't', 'a'))
                    mp4_parse_meta(data, child, m);
                pos = child.end;
            }
        }

        static bool parse_mp4(const u8 *data, std::size_t size, std::size_t start, Metadata &m)
        {
            if (!range_ok(start, 8, size))
                return false;
            bool have_ftyp = false;
            bool have_moov = false;
            Mp4Atom moov;
            std::size_t pos = start;
            while (pos < size)
            {
                Mp4Atom atom;
                if (!mp4_atom_at(data, pos, size, atom))
                    break;
                if (atom.type == mp4_fourcc('f', 't', 'y', 'p'))
                    have_ftyp = true;
                if (atom.type == mp4_fourcc('m', 'o', 'o', 'v'))
                {
                    moov = atom;
                    have_moov = true;
                }
                pos = atom.end;
            }
            if (!have_moov || (!have_ftyp && moov.start != start))
                return false;

            m.format = Format::mp4;
            pos = moov.payload;
            while (pos < moov.end)
            {
                Mp4Atom child;
                if (!mp4_atom_at(data, pos, moov.end, child))
                    break;
                if (child.type == mp4_fourcc('m', 'v', 'h', 'd'))
                    mp4_parse_mvhd(data, child, m);
                else if (child.type == mp4_fourcc('u', 'd', 't', 'a'))
                    mp4_parse_udta(data, child, m);
                else if (child.type == mp4_fourcc('m', 'e', 't', 'a'))
                    mp4_parse_meta(data, child, m);
                else if (child.type == mp4_fourcc('t', 'r', 'a', 'k'))
                    mp4_parse_trak(data, child, m);
                pos = child.end;
            }
            if (m.duration_seconds > 0.0 && size > start)
            {
                m.bitrate_kbps = static_cast<u32>(((static_cast<double>(size - start) * 8.0) / m.duration_seconds) / 1000.0 + 0.5);
            }
            return true;
        }

        // Minimal EBML/Matroska reader. The parser intentionally only descends into
        // metadata-bearing elements; Cluster payloads are skipped by their EBML size.
        struct EbmlElement
        {
            u64 id = 0;
            std::size_t header = 0;
            std::size_t payload = 0;
            std::size_t end = 0;
            bool unknown_size = false;
        };

        static bool ebml_vint(const u8 *data,
                              std::size_t pos,
                              std::size_t limit,
                              bool keep_marker,
                              u64 &value,
                              std::size_t &length,
                              bool &all_ones)
        {
            if (pos >= limit)
                return false;
            const u8 first = data[pos];
            if (!first)
                return false;

            u8 mask = 0x80;
            length = 1;
            while (length <= 8 && !(first & mask))
            {
                mask >>= 1;
                ++length;
            }
            if (length > 8 || !range_ok(pos, length, limit))
                return false;

            value = keep_marker ? first : static_cast<u64>(first & (mask - 1));
            all_ones = !keep_marker && ((first & (mask - 1)) == (mask - 1));
            for (std::size_t i = 1; i < length; ++i)
            {
                value = (value << 8) | data[pos + i];
                if (!keep_marker && data[pos + i] != 0xff)
                    all_ones = false;
            }
            return true;
        }

        static bool ebml_element_at(const u8 *data,
                                    std::size_t pos,
                                    std::size_t limit,
                                    EbmlElement &e)
        {
            u64 id = 0, size_value = 0;
            std::size_t id_len = 0, size_len = 0;
            bool dummy = false, unknown = false;
            if (!ebml_vint(data, pos, limit, true, id, id_len, dummy) || id_len > 4)
                return false;
            if (!ebml_vint(data, pos + id_len, limit, false, size_value, size_len, unknown))
                return false;
            if (id_len > std::numeric_limits<std::size_t>::max() - size_len)
                return false;
            const std::size_t header = id_len + size_len;
            if (!range_ok(pos, header, limit))
                return false;
            const std::size_t payload = pos + header;

            e.id = id;
            e.header = header;
            e.payload = payload;
            e.unknown_size = unknown;
            if (unknown)
            {
                e.end = limit;
                return true;
            }
            if (size_value > static_cast<u64>(limit - payload))
                return false;
            e.end = payload + static_cast<std::size_t>(size_value);
            return true;
        }

        static u64 ebml_uint(const u8 *p, std::size_t n)
        {
            if (!n || n > 8)
                return 0;
            u64 v = 0;
            for (std::size_t i = 0; i < n; ++i)
                v = (v << 8) | p[i];
            return v;
        }

        static double ebml_float(const u8 *p, std::size_t n)
        {
            if (n == 4)
            {
                const u32 bits = be32(p);
                float f = 0.0f;
                std::memcpy(&f, &bits, sizeof(f));
                return static_cast<double>(f);
            }
            if (n == 8)
            {
                const u64 bits = be64(p);
                double d = 0.0;
                std::memcpy(&d, &bits, sizeof(d));
                return d;
            }
            return 0.0;
        }

        static std::string ebml_utf8(const u8 *p, std::size_t n)
        {
            while (n && p[n - 1] == 0)
                --n;
            return trim_ascii(std::string(reinterpret_cast<const char *>(p), n));
        }

        static bool matroska_common_key(const std::string &raw, std::string &key)
        {
            const std::string k = upper_ascii(raw);
            key.clear();
            if (k == "TITLE")
                key = "TITLE";
            else if (k == "ARTIST" || k == "LEAD_PERFORMER" || k == "PERFORMER")
                key = "ARTIST";
            else if (k == "ALBUM")
                key = "ALBUM";
            else if (k == "ALBUMARTIST" || k == "ALBUM_ARTIST" || k == "ALBUM ARTIST")
                key = "ALBUMARTIST";
            else if (k == "COMPOSER")
                key = "COMPOSER";
            else if (k == "GENRE")
                key = "GENRE";
            else if (k == "DATE" || k == "YEAR" || k == "DATE_RELEASED" || k == "DATE_RECORDED")
                key = "DATE";
            else if (k == "COMMENT" || k == "DESCRIPTION")
                key = "COMMENT";
            else if (k == "TRACKNUMBER" || k == "TRACK_NUMBER" || k == "PART_NUMBER")
                key = "TRACKNUMBER";
            else if (k == "TRACKTOTAL" || k == "TOTALTRACKS" || k == "TOTAL_TRACKS" || k == "TOTAL_PARTS")
                key = "TRACKTOTAL";
            else if (k == "DISCNUMBER" || k == "DISC_NUMBER" || k == "DISC")
                key = "DISCNUMBER";
            else if (k == "DISCTOTAL" || k == "TOTALDISCS" || k == "TOTAL_DISCS")
                key = "DISCTOTAL";
            return !key.empty();
        }

        static void matroska_parse_simple_tag(const u8 *data,
                                              const EbmlElement &parent,
                                              Metadata &m,
                                              int depth)
        {
            if (depth > 8)
                return;
            std::string name;
            std::string value;
            std::size_t pos = parent.payload;
            std::vector<EbmlElement> nested;
            while (pos < parent.end)
            {
                EbmlElement child;
                if (!ebml_element_at(data, pos, parent.end, child) || child.end <= pos)
                    break;
                const std::size_t n = child.end - child.payload;
                if (child.id == 0x45A3)
                    name = ebml_utf8(data + child.payload, n); // TagName
                else if (child.id == 0x4487)
                    value = ebml_utf8(data + child.payload, n); // TagString
                else if (child.id == 0x67C8)
                    nested.push_back(child); // SimpleTag
                pos = child.end;
            }

            if (!name.empty() && !value.empty())
            {
                const std::string raw_key = upper_ascii(name);
                add_tag(m, raw_key, value);
                std::string common;
                if (matroska_common_key(raw_key, common))
                    apply_common(m, common, value, false);
            }
            for (std::size_t i = 0; i < nested.size(); ++i)
                matroska_parse_simple_tag(data, nested[i], m, depth + 1);
        }

        static void matroska_parse_tags(const u8 *data, const EbmlElement &tags, Metadata &m)
        {
            std::size_t pos = tags.payload;
            while (pos < tags.end)
            {
                EbmlElement tag;
                if (!ebml_element_at(data, pos, tags.end, tag) || tag.end <= pos)
                    break;
                if (tag.id == 0x7373)
                { // Tag
                    std::size_t q = tag.payload;
                    while (q < tag.end)
                    {
                        EbmlElement child;
                        if (!ebml_element_at(data, q, tag.end, child) || child.end <= q)
                            break;
                        if (child.id == 0x67C8)
                            matroska_parse_simple_tag(data, child, m, 0);
                        q = child.end;
                    }
                }
                pos = tag.end;
            }
        }

        static void matroska_parse_info(const u8 *data,
                                        const EbmlElement &info,
                                        Metadata &m,
                                        u64 &timecode_scale,
                                        double &duration_units)
        {
            std::size_t pos = info.payload;
            while (pos < info.end)
            {
                EbmlElement child;
                if (!ebml_element_at(data, pos, info.end, child) || child.end <= pos)
                    break;
                const std::size_t n = child.end - child.payload;
                if (child.id == 0x2AD7B1 && n <= 8)
                { // TimestampScale / TimecodeScale
                    const u64 v = ebml_uint(data + child.payload, n);
                    if (v)
                        timecode_scale = v;
                }
                else if (child.id == 0x4489 && (n == 4 || n == 8))
                { // Duration
                    const double v = ebml_float(data + child.payload, n);
                    if (v > 0.0)
                        duration_units = v;
                }
                else if (child.id == 0x7BA9)
                { // Title
                    const std::string title = ebml_utf8(data + child.payload, n);
                    if (!title.empty())
                        apply_common(m, "TITLE", title, false);
                }
                pos = child.end;
            }
        }

        static void matroska_parse_track_entry(const u8 *data,
                                               const EbmlElement &track,
                                               Metadata &m)
        {
            u64 track_type = 0;
            u32 sample_rate = 0;
            u32 channels = 0;
            u32 bits = 0;
            std::string codec;

            std::size_t pos = track.payload;
            while (pos < track.end)
            {
                EbmlElement child;
                if (!ebml_element_at(data, pos, track.end, child) || child.end <= pos)
                    break;
                const std::size_t n = child.end - child.payload;
                if (child.id == 0x83 && n <= 8)
                { // TrackType
                    track_type = ebml_uint(data + child.payload, n);
                }
                else if (child.id == 0x86)
                { // CodecID
                    codec = ebml_utf8(data + child.payload, n);
                }
                else if (child.id == 0xE1)
                { // Audio
                    std::size_t q = child.payload;
                    while (q < child.end)
                    {
                        EbmlElement a;
                        if (!ebml_element_at(data, q, child.end, a) || a.end <= q)
                            break;
                        const std::size_t an = a.end - a.payload;
                        if (a.id == 0xB5 && (an == 4 || an == 8))
                        { // SamplingFrequency
                            const double rate = ebml_float(data + a.payload, an);
                            if (rate > 0.0 && rate <= static_cast<double>(std::numeric_limits<u32>::max()))
                                sample_rate = static_cast<u32>(rate + 0.5);
                        }
                        else if (a.id == 0x9F && an <= 8)
                        { // Channels
                            channels = static_cast<u32>(ebml_uint(data + a.payload, an));
                        }
                        else if (a.id == 0x6264 && an <= 8)
                        { // BitDepth
                            bits = static_cast<u32>(ebml_uint(data + a.payload, an));
                        }
                        q = a.end;
                    }
                }
                pos = child.end;
            }

            if (track_type == 2)
            { // audio
                if (!m.sample_rate && sample_rate)
                    m.sample_rate = sample_rate;
                if (!m.channels && channels)
                    m.channels = channels;
                if (!m.bits_per_sample && bits)
                    m.bits_per_sample = bits;
                if (!codec.empty())
                    add_tag(m, "MATROSKA:CODEC_ID", codec);
            }
        }

        static void matroska_parse_tracks(const u8 *data, const EbmlElement &tracks, Metadata &m)
        {
            std::size_t pos = tracks.payload;
            while (pos < tracks.end)
            {
                EbmlElement child;
                if (!ebml_element_at(data, pos, tracks.end, child) || child.end <= pos)
                    break;
                if (child.id == 0xAE)
                    matroska_parse_track_entry(data, child, m); // TrackEntry
                pos = child.end;
            }
        }

        static u32 matroska_picture_type(const std::string &filename, const std::string &description)
        {
            const std::string text = lower_ascii(filename + " " + description);
            if (text.find("back") != std::string::npos)
                return 4;
            if (text.find("front") != std::string::npos ||
                text.find("cover") != std::string::npos ||
                text.find("folder") != std::string::npos ||
                text.find("albumart") != std::string::npos ||
                text.find("album art") != std::string::npos)
                return 3;
            return 0;
        }

        static void matroska_parse_attached_file(const u8 *data,
                                                 const EbmlElement &file,
                                                 Metadata &m)
        {
            std::string description;
            std::string filename;
            std::string mime;
            const u8 *file_data = nullptr;
            std::size_t file_size = 0;

            std::size_t pos = file.payload;
            while (pos < file.end)
            {
                EbmlElement child;
                if (!ebml_element_at(data, pos, file.end, child) || child.end <= pos)
                    break;
                const std::size_t n = child.end - child.payload;
                if (child.id == 0x467E)
                    description = ebml_utf8(data + child.payload, n); // FileDescription
                else if (child.id == 0x466E)
                    filename = ebml_utf8(data + child.payload, n); // FileName
                else if (child.id == 0x4660)
                    mime = ebml_utf8(data + child.payload, n); // FileMimeType
                else if (child.id == 0x465C)
                { // FileData
                    file_data = data + child.payload;
                    file_size = n;
                }
                pos = child.end;
            }

            if (!file_data || !file_size)
                return;
            if (mime.empty())
                mime = mime_from_image(file_data, file_size);
            const std::string lmime = lower_ascii(mime);
            if (lmime.compare(0, 6, "image/") != 0 && mime_from_image(file_data, file_size) == "application/octet-stream")
                return;

            Picture pic;
            pic.type = matroska_picture_type(filename, description);
            pic.mime = lmime.compare(0, 6, "image/") == 0 ? mime : mime_from_image(file_data, file_size);
            pic.description = !description.empty() ? description : filename;
            pic.data.assign(file_data, file_data + file_size);
            fill_image_dimensions(pic);
            m.pictures.push_back(std::move(pic));
        }

        static void matroska_parse_attachments(const u8 *data,
                                               const EbmlElement &attachments,
                                               Metadata &m)
        {
            std::size_t pos = attachments.payload;
            while (pos < attachments.end)
            {
                EbmlElement child;
                if (!ebml_element_at(data, pos, attachments.end, child) || child.end <= pos)
                    break;
                if (child.id == 0x61A7)
                    matroska_parse_attached_file(data, child, m); // AttachedFile
                pos = child.end;
            }
        }

        static bool parse_matroska(const u8 *data, std::size_t size, std::size_t start, Metadata &m)
        {
            if (!range_ok(start, 4, size) || be32(data + start) != 0x1A45DFA3u)
                return false;

            EbmlElement header;
            if (!ebml_element_at(data, start, size, header) || header.id != 0x1A45DFA3u || header.unknown_size)
                return false;
            std::string doc_type;
            std::size_t pos = header.payload;
            while (pos < header.end)
            {
                EbmlElement child;
                if (!ebml_element_at(data, pos, header.end, child) || child.end <= pos)
                    return false;
                if (child.id == 0x4282)
                    doc_type = lower_ascii(ebml_utf8(data + child.payload, child.end - child.payload));
                pos = child.end;
            }
            if (doc_type != "matroska" && doc_type != "webm")
                return false;

            pos = header.end;
            EbmlElement segment;
            bool have_segment = false;
            while (pos < size)
            {
                EbmlElement e;
                if (!ebml_element_at(data, pos, size, e) || e.end <= pos)
                    break;
                if (e.id == 0x18538067u)
                {
                    segment = e;
                    have_segment = true;
                    break;
                }
                pos = e.end;
            }
            if (!have_segment)
                return false;

            m.format = Format::matroska;
            add_tag(m, "MATROSKA:DOC_TYPE", doc_type);
            u64 timecode_scale = 1000000; // nanoseconds per segment tick
            double duration_units = 0.0;

            pos = segment.payload;
            while (pos < segment.end)
            {
                EbmlElement child;
                if (!ebml_element_at(data, pos, segment.end, child) || child.end <= pos)
                    break;
                if (child.id == 0x1549A966u)
                    matroska_parse_info(data, child, m, timecode_scale, duration_units);
                else if (child.id == 0x1654AE6Bu)
                    matroska_parse_tracks(data, child, m);
                else if (child.id == 0x1254C367u)
                    matroska_parse_tags(data, child, m);
                else if (child.id == 0x1941A469u)
                    matroska_parse_attachments(data, child, m);
                // SeekHead, Cues, Chapters, Clusters and unknown elements are skipped.
                if (child.unknown_size)
                    break;
                pos = child.end;
            }

            if (duration_units > 0.0 && timecode_scale)
                m.duration_seconds = duration_units * static_cast<double>(timecode_scale) / 1000000000.0;
            if (m.duration_seconds > 0.0 && size > start)
                m.bitrate_kbps = static_cast<u32>(((static_cast<double>(size - start) * 8.0) / m.duration_seconds) / 1000.0 + 0.5);
            return true;
        }

        static bool parse_mpeg_audio(const u8 *data, std::size_t size, std::size_t start, Metadata &m)
        {
            std::size_t end = end_without_id3v1(data, size);
            if (end >= 32 && std::memcmp(data + end - 32, "APETAGEX", 8) == 0)
            {
                const u32 ape_size = le32(data + end - 20);
                if (ape_size >= 32 && ape_size <= end)
                    end -= ape_size;
            }
            if (start >= end)
                return false;

            MpegFrameInfo first;
            const std::size_t first_pos = find_first_mpeg_frame(data, start, end, first);
            if (first_pos == end)
                return false;
            std::size_t pos = first_pos;
            m.format = Format::mpeg_audio;
            m.sample_rate = first.sample_rate;
            m.channels = first.channels;

            u64 total_samples = 0;
            u64 bitrate_sum = 0;
            u64 frame_count = 0;
            while (pos + 4 <= end)
            {
                MpegFrameInfo f;
                if (!parse_mpeg_header(data + pos, end - pos, f) || f.sample_rate != first.sample_rate || pos + f.frame_bytes > end)
                {
                    // A few encoders insert small junk blocks; resync conservatively.
                    MpegFrameInfo found;
                    const std::size_t next = find_first_mpeg_frame(data, pos + 1, std::min(end, pos + 4096), found);
                    if (next >= std::min(end, pos + 4096))
                        break;
                    pos = next;
                    continue;
                }
                total_samples += f.samples;
                bitrate_sum += f.bitrate_kbps;
                ++frame_count;
                pos += f.frame_bytes;
            }
            if (!frame_count)
                return false;
            u64 playable_samples = total_samples;
            u64 xing_samples = 0;
            if (mpeg_gapless_samples_from_xing(data + first_pos, first.frame_bytes, first, xing_samples))
            {
                playable_samples = xing_samples;
            }
            m.duration_seconds = static_cast<double>(playable_samples) / first.sample_rate;
            m.bitrate_kbps = static_cast<u32>((bitrate_sum + frame_count / 2) / frame_count);
            return true;
        }

    } // namespace

    static bool parse_wave_tags(const u8 *data, std::size_t size, Metadata &out)
    {
        if (size < 12 || std::memcmp(data, "RIFF", 4) || std::memcmp(data + 8, "WAVE", 4))
            return false;
        const u64 declared = u64(le32(data + 4)) + 8;
        if (declared < 12 || declared > size)
            return false;
        const auto end = static_cast<std::size_t>(declared);
        out.format = Format::wave;
        for (std::size_t pos = 12; range_ok(pos, 8, end);)
        {
            const auto count = le32(data + pos + 4);
            const auto begin = pos + 8;
            if (!range_ok(begin, count, end))
                return false;
            if (!std::memcmp(data + pos, "LIST", 4) && count >= 4 && !std::memcmp(data + begin, "INFO", 4))
            {
                const auto list_end = begin + count;
                for (std::size_t item = begin + 4; range_ok(item, 8, list_end);)
                {
                    const auto length = le32(data + item + 4);
                    if (!range_ok(item + 8, length, list_end))
                        return false;
                    const char *key = nullptr;
                    if (!std::memcmp(data + item, "INAM", 4))
                        key = "TITLE";
                    else if (!std::memcmp(data + item, "IART", 4))
                        key = "ARTIST";
                    else if (!std::memcmp(data + item, "IPRD", 4))
                        key = "ALBUM";
                    if (key)
                        apply_common(out, key, trim_ascii(latin1_fixed(data + item + 8, length)));
                    item += 8 + std::size_t(length) + (length & 1);
                }
            }
            else if (!std::memcmp(data + pos, "id3 ", 4) || !std::memcmp(data + pos, "ID3 ", 4))
            {
                parse_id3v2(data + begin, count, out);
            }
            pos = begin + count + (count & 1);
        }
        return true;
    }

    void Metadata::clear()
    {
        *this = Metadata{};
    }

    const std::vector<std::string> *Metadata::values(const std::string &key) const
    {
        const auto it = tags.find(upper_ascii(key));
        return it == tags.end() ? nullptr : &it->second;
    }

    std::string Metadata::value(const std::string &key, std::size_t index) const
    {
        const auto *v = values(key);
        return (!v || index >= v->size()) ? std::string() : (*v)[index];
    }

    const Picture *Metadata::front_cover() const
    {
        for (const Picture &picture : pictures)
        {
            if (picture.type == 3)
                return &picture;
        }
        return pictures.empty() ? nullptr : &pictures.front();
    }

    bool read_memory(const void *ptr, std::size_t size, Metadata &out, std::string *error)
    {
        out.clear();
        if (!ptr || size == 0)
        {
            set_error(error, "empty input");
            return false;
        }
        const u8 *data = static_cast<const u8 *>(ptr);

        std::size_t audio_start = 0;
        if (size >= 10 && std::memcmp(data, "ID3", 3) == 0)
        {
            audio_start = parse_id3v2(data, size, out);
            if (!audio_start)
            {
                set_error(error, "invalid ID3v2 tag");
                return false;
            }
        }

        bool recognized = false;
        if (range_ok(audio_start, 4, size) && futurecomposer::signature(data + audio_start, size - audio_start))
        {
            recognized = futurecomposer::valid(data + audio_start, size - audio_start);
            if (!recognized)
            {
                set_error(error, "invalid Future Composer module");
                return false;
            }
            out.format = Format::future_composer;
            out.channels = 4;
            out.tags["FORMAT"].push_back(data[audio_start] == 'S' ? "Future Composer 1.0-1.3" : "Future Composer 1.4");
        }
        else if (range_ok(audio_start, 4, size) && std::memcmp(data + audio_start, "RIFF", 4) == 0)
        {
            recognized = parse_wave_tags(data + audio_start, size - audio_start, out);
        }
        else if (range_ok(audio_start, 4, size) && std::memcmp(data + audio_start, "fLaC", 4) == 0)
        {
            recognized = parse_flac(data, size, audio_start, out);
        }
        else if (range_ok(audio_start, 4, size) && std::memcmp(data + audio_start, "OggS", 4) == 0)
        {
            recognized = parse_ogg(data, size, audio_start, out);
        }
        else if (range_ok(audio_start, 4, size) && std::memcmp(data + audio_start, "MAC ", 4) == 0)
        {
            out.format = Format::monkeys_audio;
            parse_monkeys_audio_info(data, size, audio_start, out);
            recognized = true;
        }
        else if (parse_mp4(data, size, audio_start, out))
        {
            recognized = true;
        }
        else if (parse_matroska(data, size, audio_start, out))
        {
            recognized = true;
        }
        else if (parse_xm(data, size, audio_start, out))
        {
            recognized = true;
        }
        else if (parse_it(data, size, audio_start, out))
        {
            recognized = true;
        }
        else if (parse_s3m(data, size, audio_start, out))
        {
            recognized = true;
        }
        else if (parse_mod(data, size, audio_start, out))
        {
            recognized = true;
        }
        else
        {
            recognized = parse_mpeg_audio(data, size, audio_start, out);
        }

        // APEv1/v2 can legally appear on formats other than Monkey's Audio
        // (notably MP3, Musepack and WavPack), so parse a trailing APE tag
        // independently of container recognition.
        parse_ape_tags(data, size, out, out.format == Format::monkeys_audio || !recognized);
        parse_id3v1(data, size, out);
        if (!recognized)
        {
            // Standalone/tag-only data is still useful if a recognized tag was parsed.
            const bool has_tags = !out.tags.empty() || !out.pictures.empty();
            if (!has_tags)
            {
                set_error(error, "unrecognized or unsupported audio stream");
                return false;
            }
        }
        if (error)
            error->clear();
        return true;
    }

    bool read_file(const std::string &path, Metadata &out, std::string *error)
    {
        out.clear();
        std::ifstream f(std::filesystem::path(reinterpret_cast<const char8_t *>(path.c_str())), std::ios::binary);
        if (!f)
        {
            set_error(error, "could not open file");
            return false;
        }
        f.seekg(0, std::ios::end);
        const std::streamoff end = f.tellg();
        if (end <= 0)
        {
            set_error(error, "empty or unreadable file");
            return false;
        }
        if (static_cast<unsigned long long>(end) > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()))
        {
            set_error(error, "file too large for address space");
            return false;
        }
        f.seekg(0, std::ios::beg);
        std::vector<u8> bytes(static_cast<std::size_t>(end));
        if (!f.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        {
            set_error(error, "could not read complete file");
            return false;
        }
        return read_memory(bytes.data(), bytes.size(), out, error);
    }

    const char *format_name(Format format)
    {
        switch (format)
        {
        case Format::mpeg_audio:
            return "MPEG audio";
        case Format::flac:
            return "FLAC";
        case Format::ogg_vorbis:
            return "Ogg Vorbis";
        case Format::opus:
            return "Opus";
        case Format::mp4:
            return "MP4/M4A";
        case Format::matroska:
            return "Matroska/WebM";
        case Format::mod:
            return "MOD";
        case Format::s3m:
            return "S3M";
        case Format::xm:
            return "XM";
        case Format::it:
            return "IT";
        case Format::monkeys_audio:
            return "Monkey's Audio";
        case Format::wave:
            return "RIFF/WAVE";
        case Format::future_composer:
            return "Future Composer";
        default:
            return "Unknown";
        }
    }

} // namespace audiotags
