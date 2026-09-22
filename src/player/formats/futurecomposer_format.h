#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace futurecomposer
{
    inline bool signature(const uint8_t *data, size_t size)
    {
        return size >= 4 && (!std::memcmp(data, "FC14", 4) || !std::memcmp(data, "SMOD", 4));
    }
    inline uint32_t be32(const uint8_t *p)
    {
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }
    inline bool valid(const uint8_t *data, size_t size)
    {
        if (!signature(data, size))
            return false;
        const bool old = data[0] == 'S';
        const size_t header = old ? 100 : 180;
        if (size < header)
            return false;
        const auto range = [size](size_t offset, size_t length)
        { return offset <= size && length <= size - offset; };
        const size_t patterns = be32(data + 8);
        size_t tracks = be32(data + 4);
        if (!tracks && patterns >= header)
            tracks = patterns - header; // Historical SMOD workaround.
        if (!tracks || tracks % 13 || !range(header, tracks))
            return false;
        for (unsigned field : {8u, 16u, 24u})
        {
            const size_t offset = be32(data + field), length = be32(data + field + 4);
            if (offset < header || !length || length % 64 || !range(offset, length))
                return false;
        }
        size_t samples = be32(data + 32);
        for (unsigned i = 0; i < 10; ++i)
        {
            const auto h = data + 40 + i * 6;
            const size_t length = ((size_t(h[0]) << 8) | h[1]) * 2;
            if (!range(samples, length))
                return false;
            samples += length;
            if (!old && length)
                samples += 2;
        }
        if (!old)
        {
            size_t waves = be32(data + 36);
            for (unsigned i = 0; i < 80; ++i)
            {
                const size_t length = size_t(data[100 + i]) * 2;
                if (!range(waves, length))
                    return false;
                waves += length;
            }
        }
        return true;
    }
}
