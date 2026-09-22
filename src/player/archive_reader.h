#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct archive_member { std::string name; std::vector<uint8_t> bytes; };
bool is_music_archive(const std::string &name);
std::vector<archive_member> read_music_archive(const std::vector<uint8_t> &bytes,
                                             const std::string &name,
                                             size_t member_limit = 256 * 1024 * 1024);
