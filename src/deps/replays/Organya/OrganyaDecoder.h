#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace Organya
{
    struct Stream;
}
// namespace Organya

namespace Organya
{
    struct Song;

    void initialize();
    void release();
    Song* Load(const uint8_t* bytes, size_t size, uint32_t sampleRate);
    void Unload(Song* song);
    void Reset(Song* song);
    uint32_t GetDuration(Song* song);
    int32_t GetVersion(Song* song);
    std::vector<float> Render(Song* song);
}
// namespace Organya