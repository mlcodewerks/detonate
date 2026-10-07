#pragma once
#include <filesystem>

void midi_set_system_directory(const std::filesystem::path &directory);
std::filesystem::path midi_resource_directory();
