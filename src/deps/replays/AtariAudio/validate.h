#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>

// Check serialized bounds before the upstream parsers retain pointers into the file.
namespace atari_input
{
inline uint32_t be(const uint8_t* p, unsigned n)
{
    uint32_t v = 0;
    while (n--) v = (v << 8) | *p++;
    return v;
}
inline bool sndh(const void* data, uint32_t size)
{
    if (!data || size < 18 || size > 3 * 1024 * 1024) return false;
    const auto* b = static_cast<const uint8_t*>(data);
    if (b[0] != 0x60 || std::memcmp(b + 12, "SNDH", 4)) return false;
    if (b[1] >= 128) return false;
    size_t end = (b[1] ? b[1] : be(b + 2, 2)) + 2;
    if (end < 16 || end > size) return false;
    unsigned songs = 1;
    for (size_t p = 16; p + 4 <= end;)
    {
        auto tag = [&](const char* s, unsigned n = 4) { return !std::memcmp(b + p, s, n); };
        if (tag("HDNS")) break;
        if (tag("##", 2))
        {
            if (b[p + 2] < '0' || b[p + 2] > '9' || b[p + 3] < '0' || b[p + 3] > '9') return false;
            songs = (b[p + 2] - '0') * 10 + b[p + 3] - '0';
            if (!songs) songs = 1;
            p += 4;
        }
        else if (tag("TIME") || tag("FRMS") || tag("!#SN"))
        {
            const bool time = tag("TIME"), frames = tag("FRMS");
            p += 4;
            if (time && (p & 1)) ++p;
            size_t n = songs * (frames ? 4 : 2);
            if (p > end || n > end - p) return false;
            p += n;
        }
        else if (tag("TITL") || tag("COMM") || tag("RIPP") || tag("CONV") || tag("YEAR") ||
                 tag("!#", 2) || tag("TA", 2) || tag("TB", 2) || tag("TC", 2) || tag("TD", 2) || tag("!V", 2))
        {
            const bool tick = tag("TA", 2) || tag("TB", 2) || tag("TC", 2) || tag("TD", 2) || tag("!V", 2);
            p += (tick || tag("!#", 2)) ? 2 : 4;
            const auto* zero = static_cast<const uint8_t*>(std::memchr(b + p, 0, end - p));
            if (!zero) return false;
            if (tick)
            {
                long hz = std::strtol(reinterpret_cast<const char*>(b + p), nullptr, 10);
                if (hz <= 0 || hz > 2000) return false;
            }
            p = size_t(zero - b) + 1;
        }
        else ++p;
    }
    return true;
}
inline bool ym(const void* data, uint32_t size)
{
    if (!data || size < 18 || size > 16 * 1024 * 1024) return false;
    const auto* b = static_cast<const uint8_t*>(data);
    auto tag = [&](const char* s) { return !std::memcmp(b, s, 4); };
    if (tag("YM2!") || tag("YM3!") || tag("YM3b"))
    {
        size_t prefix = tag("YM3b") ? 8 : 4;
        return size > prefix && (size - prefix) % 14 == 0;
    }
    if (size < 34 || std::memcmp(b + 4, "LeOnArD!", 8)) return false;
    size_t p = 0;
    auto take = [&](size_t n) { if (p > size || n > size - p) return false; p += n; return true; };
    auto text = [&]() {
        for (int i = 0; i < 3; ++i) {
            if (p >= size) return false;
            const auto* z = static_cast<const uint8_t*>(std::memchr(b + p, 0, size - p));
            if (!z) return false;
            p = size_t(z - b) + 1;
        }
        return true;
    };
    if (tag("YM5!") || tag("YM6!"))
    {
        uint32_t frames = be(b + 12, 4), clock = be(b + 22, 4), hz = be(b + 26, 2);
        unsigned samples = be(b + 20, 2);
        if (!frames || samples > 64 || clock < 8 || clock > 32000000 || !hz || hz > 2000) return false;
        p = 34;
        if (!take(be(b + 32, 2))) return false;
        for (unsigned i = 0; i < samples; ++i) {
            if (size - p < 4) return false;
            uint32_t n = be(b + p, 4);
            if (!n || !take(4) || !take(n)) return false;
        }
        return text() && uint64_t(frames) * 16 <= size - p && uint64_t(frames) * 44100 / hz <= UINT32_MAX;
    }
    if (tag("MIX1"))
    {
        uint32_t bank = be(b + 16, 4), patterns = be(b + 20, 4);
        p = 24;
        if (!patterns || !take(uint64_t(patterns) * 12)) return false;
        for (size_t q = 24; q < p; q += 12) {
            uint32_t start = be(b + q, 4), len = be(b + q + 4, 4);
            if (!len || start > bank || len > bank - start || !be(b + q + 8, 2) || !be(b + q + 10, 2)) return false;
        }
        return text() && bank <= size - p;
    }
    if (tag("YMT1") || tag("YMT2"))
    {
        unsigned voices = be(b + 12, 2), hz = be(b + 14, 2), samples = be(b + 24, 2);
        uint32_t frames = be(b + 16, 4), flags = be(b + 26, 4);
        if (!voices || voices > 8 || !hz || hz > 2000 || !frames || samples > 64 || (flags & 4)) return false;
        p = 30;
        if (!text()) return false;
        for (unsigned i = 0; i < samples; ++i) {
            size_t header = tag("YMT2") ? 6 : 2;
            if (size - p < header) return false;
            uint32_t n = be(b + p, 2);
            if (!n || !take(header) || !take(n)) return false;
        }
        return uint64_t(frames) * voices * 4 <= size - p && uint64_t(frames) * 44100 / hz <= UINT32_MAX;
    }
    return false;
}
}
