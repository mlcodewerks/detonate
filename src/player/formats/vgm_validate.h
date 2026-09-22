#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

// Check container offsets and command boundaries before the upstream engine
// follows pointers. Command sizes follow this revision's VGMPlayer::_CMD_INFO.
inline bool valid_vgm(const std::vector<uint8_t> &b)
{
    if (b.size() < 0x40 || std::memcmp(b.data(), "Vgm ", 4))
        return false;
    auto u32 = [&](size_t p) -> uint32_t
    {
        return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 | uint32_t(b[p + 3]) << 24;
    };
    auto rel = [&](size_t p) -> size_t
    { return u32(p) ? size_t(u32(p)) + p : 0; };
    const size_t eof = rel(4) ? rel(4) : b.size();
    if (eof > b.size() || eof <= 0x40)
        return false;
    size_t start = u32(8) >= 0x150 ? rel(0x34) : 0;
    if (!start)
        start = 0x40;
    if (start < 0x40 || start >= eof)
        return false;
    const size_t tags = rel(0x14), loop = rel(0x1c);
    size_t end = eof;
    if (tags)
    {
        if (tags > eof || eof - tags < 12 || std::memcmp(b.data() + tags, "Gd3 ", 4))
            return false;
        const size_t length = u32(tags + 8);
        if (length > eof - tags - 12 || (length & 1))
            return false;
        if (tags >= start)
            end = tags;
    }
    if (start >= 0xc0)
    {
        const size_t extra = rel(0xbc);
        if (extra && (extra > eof || eof - extra < 4 || u32(extra) > eof - extra || u32(extra) < 4))
            return false;
        if (extra)
            for (size_t field : {size_t(4), size_t(8)})
            {
                if (u32(extra) >= field + 4 && rel(extra + field) >= eof)
                    return false;
            }
    }
    size_t p = start;
    uint64_t ticks = 0, loop_ticks = 0;
    bool loop_found = !loop;
    while (p < end)
    {
        if (p == loop)
        {
            loop_found = true;
            loop_ticks = ticks;
        }
        const unsigned cmd = b[p];
        size_t length = 0;
        if (cmd == 0)
            length = 1;
        else if (cmd >= 0x30 && cmd <= 0x3f)
            length = 2;
        else if (cmd >= 0x40 && cmd <= 0x5f)
            length = cmd == 0x4f || cmd == 0x50 ? 2 : 3;
        else if (cmd == 0x61)
            length = 3;
        else if (cmd == 0x62 || cmd == 0x63 || (cmd >= 0x70 && cmd <= 0x8f))
            length = 1;
        else if (cmd == 0x68)
            length = 12;
        else if (cmd >= 0x90 && cmd <= 0x95)
        {
            constexpr unsigned lengths[] = {5, 5, 6, 11, 2, 5};
            length = lengths[cmd - 0x90];
        }
        else if (cmd >= 0xa0 && cmd <= 0xbf)
            length = 3;
        else if (cmd >= 0xc0 && cmd <= 0xdf)
            length = 4;
        else if (cmd >= 0xe0)
            length = 5;
        else if (cmd == 0x66)
            return loop_found && (!loop || ticks > loop_ticks);
        else if (cmd == 0x67)
        {
            if (end - p < 7 || b[p + 1] != 0x66)
                return false;
            const size_t size = u32(p + 3) & 0x7fffffff;
            if (size > end - p - 7)
                return false;
            length = 7 + size;
            if (b[p + 2] >= 0x40 && b[p + 2] < 0x7f)
            {
                if (size < 10 || b[p + 7] > 1 || u32(p + 8) > 256 * 1024 * 1024)
                    return false;
            }
        }
        if (!length || length > end - p)
            return false;
        if (cmd == 0x61)
            ticks += unsigned(b[p + 1]) | unsigned(b[p + 2]) << 8;
        else if (cmd == 0x62)
            ticks += 735;
        else if (cmd == 0x63)
            ticks += 882;
        else if (cmd >= 0x70 && cmd <= 0x7f)
            ticks += (cmd & 15) + 1;
        else if (cmd >= 0x80 && cmd <= 0x8f)
            ticks += cmd & 15;
        p += length;
    }
    return false; // missing end command
}
