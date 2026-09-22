#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace ken {
struct player {
    virtual ~player() = default;
    virtual unsigned duration() const = 0;
    virtual void render(int16_t *pcm, unsigned frames) = 0;
};
std::unique_ptr<player> load(const std::string &ext, const std::vector<uint8_t> &data,
                           const std::vector<uint8_t> &waves, unsigned rate);
}
